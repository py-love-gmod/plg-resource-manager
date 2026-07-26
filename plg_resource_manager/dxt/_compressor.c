#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

// Заголовочная часть
#define STB_DXT_NORMAL    0
#define STB_DXT_DITHER    1
#define STB_DXT_HIGHQUAL  2

#define STBD_ABS(i)   abs(i)
#define STBD_FABS(x)  fabs(x)
#define STBD_MEMSET   memset

int stb__Mul8Bit(int a, int b);
void stb__From16Bit(unsigned char *out, unsigned short v);
unsigned short stb__As16Bit(int r, int g, int b);
int stb__Lerp13(int a, int b);
void stb__Lerp13RGB(unsigned char *out, unsigned char *p1, unsigned char *p2);
void stb__PrepareOptTable(unsigned char *Table, const unsigned char *expand, int size);
void stb__EvalColors(unsigned char *color, unsigned short c0, unsigned short c1);
void stb__DitherBlock(unsigned char *dest, unsigned char *block);
unsigned int stb__MatchColorsBlock(unsigned char *block, unsigned char *color, int dither);
void stb__OptimizeColorsBlock(unsigned char *block, unsigned short *pmax16, unsigned short *pmin16);
int stb__sclamp(float y, int p0, int p1);
int stb__RefineBlock(unsigned char *block, unsigned short *pmax16, unsigned short *pmin16, unsigned int mask);
void stb__CompressColorBlock(unsigned char *dest, unsigned char *block, int mode);
void stb__CompressAlphaBlock(unsigned char *dest, unsigned char *src, int stride);
void stb__InitDXT(void);
void stb_compress_dxt_block(unsigned char *dest, const unsigned char *src, int alpha, int mode);
void stb_compress_bc4_block(unsigned char *dest, const unsigned char *src);
void stb_compress_bc5_block(unsigned char *dest, const unsigned char *src);
int imin(int x, int y);
void extract_block(const unsigned char *src, int x, int y, int w, int h, unsigned char *block);
uint64_t get_compress_pixels_dst_size_bytes(uint64_t w, uint64_t h, bool is_rgba);
bool compress_pixels(unsigned char *dst, const unsigned char *src, uint64_t w, uint64_t h, bool is_rgba);

// Реализация сжатия
static unsigned char stb__Expand5[32];
static unsigned char stb__Expand6[64];
static unsigned char stb__OMatch5[256][2];
static unsigned char stb__OMatch6[256][2];
static unsigned char stb__QuantRBTab[256+16];
static unsigned char stb__QuantGTab[256+16];

int stb__Mul8Bit(int a, int b)
{
  int t = a*b + 128;
  return (t + (t >> 8)) >> 8;
}

void stb__From16Bit(unsigned char *out, unsigned short v)
{
   int rv = (v & 0xf800) >> 11;
   int gv = (v & 0x07e0) >>  5;
   int bv = (v & 0x001f) >>  0;

   out[0] = stb__Expand5[rv];
   out[1] = stb__Expand6[gv];
   out[2] = stb__Expand5[bv];
   out[3] = 0;
}

unsigned short stb__As16Bit(int r, int g, int b)
{
   return (stb__Mul8Bit(r,31) << 11) + (stb__Mul8Bit(g,63) << 5) + stb__Mul8Bit(b,31);
}

int stb__Lerp13(int a, int b)
{
   return (2*a + b) / 3;
}

void stb__Lerp13RGB(unsigned char *out, unsigned char *p1, unsigned char *p2)
{
   out[0] = stb__Lerp13(p1[0], p2[0]);
   out[1] = stb__Lerp13(p1[1], p2[1]);
   out[2] = stb__Lerp13(p1[2], p2[2]);
}

void stb__PrepareOptTable(unsigned char *Table, const unsigned char *expand, int size)
{
   int i, mn, mx;
   for (i = 0; i < 256; i++) {
      int bestErr = 256;
      for (mn = 0; mn < size; mn++) {
         for (mx = 0; mx < size; mx++) {
            int mine = expand[mn];
            int maxe = expand[mx];
            int err = STBD_ABS(stb__Lerp13(maxe, mine) - i);
            err += STBD_ABS(maxe - mine) * 3 / 100;
            if (err < bestErr) {
               Table[i*2+0] = mx;
               Table[i*2+1] = mn;
               bestErr = err;
            }
         }
      }
   }
}

void stb__EvalColors(unsigned char *color, unsigned short c0, unsigned short c1)
{
   stb__From16Bit(color+ 0, c0);
   stb__From16Bit(color+ 4, c1);
   stb__Lerp13RGB(color+ 8, color+0, color+4);
   stb__Lerp13RGB(color+12, color+4, color+0);
}

void stb__DitherBlock(unsigned char *dest, unsigned char *block)
{
  int err[8], *ep1 = err, *ep2 = err+4, *et;
  int ch, y;
  for (ch = 0; ch < 3; ++ch) {
      unsigned char *bp = block+ch, *dp = dest+ch;
      unsigned char *quant = (ch == 1) ? stb__QuantGTab+8 : stb__QuantRBTab+8;
      STBD_MEMSET(err, 0, sizeof(err));
      for (y = 0; y < 4; ++y) {
         dp[ 0] = quant[bp[ 0] + ((3*ep2[1] + 5*ep2[0]) >> 4)];
         ep1[0] = bp[ 0] - dp[ 0];
         dp[ 4] = quant[bp[ 4] + ((7*ep1[0] + 3*ep2[2] + 5*ep2[1] + ep2[0]) >> 4)];
         ep1[1] = bp[ 4] - dp[ 4];
         dp[ 8] = quant[bp[ 8] + ((7*ep1[1] + 3*ep2[3] + 5*ep2[2] + ep2[1]) >> 4)];
         ep1[2] = bp[ 8] - dp[ 8];
         dp[12] = quant[bp[12] + ((7*ep1[2] + 5*ep2[3] + ep2[2]) >> 4)];
         ep1[3] = bp[12] - dp[12];
         bp += 16;
         dp += 16;
         et = ep1, ep1 = ep2, ep2 = et;
      }
   }
}

unsigned int stb__MatchColorsBlock(unsigned char *block, unsigned char *color, int dither)
{
   unsigned int mask = 0;
   int dirr = color[0*4+0] - color[1*4+0];
   int dirg = color[0*4+1] - color[1*4+1];
   int dirb = color[0*4+2] - color[1*4+2];
   int dots[16];
   int stops[4];
   int i;
   int c0Point, halfPoint, c3Point;

   for (i = 0; i < 16; i++)
      dots[i] = block[i*4+0]*dirr + block[i*4+1]*dirg + block[i*4+2]*dirb;

   for (i = 0; i < 4; i++)
      stops[i] = color[i*4+0]*dirr + color[i*4+1]*dirg + color[i*4+2]*dirb;

   c0Point   = (stops[1] + stops[3]) >> 1;
   halfPoint = (stops[3] + stops[2]) >> 1;
   c3Point   = (stops[2] + stops[0]) >> 1;

   if (!dither) {
      for (i = 15; i >= 0; i--) {
         int dot = dots[i];
         mask <<= 2;
         if (dot < halfPoint)
           mask |= (dot < c0Point) ? 1 : 3;
         else
           mask |= (dot < c3Point) ? 2 : 0;
      }
   } else {
      int err[8], *ep1 = err, *ep2 = err+4;
      int *dp = dots, y;
      c0Point   <<= 4;
      halfPoint <<= 4;
      c3Point   <<= 4;
      for (i = 0; i < 8; i++) err[i] = 0;
      for (y = 0; y < 4; y++) {
         int dot, lmask, step;
         dot = (dp[0] << 4) + (3*ep2[1] + 5*ep2[0]);
         step = (dot < halfPoint) ? ((dot < c0Point) ? 1 : 3) : ((dot < c3Point) ? 2 : 0);
         ep1[0] = dp[0] - stops[step];
         lmask = step;

         dot = (dp[1] << 4) + (7*ep1[0] + 3*ep2[2] + 5*ep2[1] + ep2[0]);
         step = (dot < halfPoint) ? ((dot < c0Point) ? 1 : 3) : ((dot < c3Point) ? 2 : 0);
         ep1[1] = dp[1] - stops[step];
         lmask |= step<<2;

         dot = (dp[2] << 4) + (7*ep1[1] + 3*ep2[3] + 5*ep2[2] + ep2[1]);
         step = (dot < halfPoint) ? ((dot < c0Point) ? 1 : 3) : ((dot < c3Point) ? 2 : 0);
         ep1[2] = dp[2] - stops[step];
         lmask |= step<<4;

         dot = (dp[3] << 4) + (7*ep1[2] + 5*ep2[3] + ep2[2]);
         step = (dot < halfPoint) ? ((dot < c0Point) ? 1 : 3) : ((dot < c3Point) ? 2 : 0);
         ep1[3] = dp[3] - stops[step];
         lmask |= step<<6;

         dp += 4;
         mask |= lmask << (y*8);
         { int *et = ep1; ep1 = ep2; ep2 = et; }
      }
   }
   return mask;
}

void stb__OptimizeColorsBlock(unsigned char *block, unsigned short *pmax16, unsigned short *pmin16)
{
  int mind = 0x7fffffff, maxd = -0x7fffffff;
  unsigned char *minp = NULL, *maxp = NULL;
  int v_r, v_g, v_b;
  static const int nIterPower = 4;
  float covf[6], vfr, vfg, vfb;
  int cov[6], mu[3], min[3], max[3];
  int ch, i, iter;

  for (ch = 0; ch < 3; ch++) {
    const unsigned char *bp = block + ch;
    int muv, minv, maxv;
    muv = minv = maxv = bp[0];
    for (i = 4; i < 64; i += 4) {
      muv += bp[i];
      if (bp[i] < minv) minv = bp[i];
      else if (bp[i] > maxv) maxv = bp[i];
    }
    mu[ch] = (muv + 8) >> 4;
    min[ch] = minv;
    max[ch] = maxv;
  }

  for (i = 0; i < 6; i++) cov[i] = 0;
  for (i = 0; i < 16; i++) {
    int r = block[i*4+0] - mu[0];
    int g = block[i*4+1] - mu[1];
    int b = block[i*4+2] - mu[2];
    cov[0] += r*r;
    cov[1] += r*g;
    cov[2] += r*b;
    cov[3] += g*g;
    cov[4] += g*b;
    cov[5] += b*b;
  }

  for (i = 0; i < 6; i++)
    covf[i] = cov[i] / 255.0f;

  vfr = (float)(max[0] - min[0]);
  vfg = (float)(max[1] - min[1]);
  vfb = (float)(max[2] - min[2]);

  for (iter = 0; iter < nIterPower; iter++) {
    float r = vfr*covf[0] + vfg*covf[1] + vfb*covf[2];
    float g = vfr*covf[1] + vfg*covf[3] + vfb*covf[4];
    float b = vfr*covf[2] + vfg*covf[4] + vfb*covf[5];
    vfr = r; vfg = g; vfb = b;
  }

  double magn = STBD_FABS(vfr);
  if (STBD_FABS(vfg) > magn) magn = STBD_FABS(vfg);
  if (STBD_FABS(vfb) > magn) magn = STBD_FABS(vfb);

  if (magn < 4.0f) {
      v_r = 299; v_g = 587; v_b = 114;
  } else {
      magn = 512.0 / magn;
      v_r = (int)(vfr * magn);
      v_g = (int)(vfg * magn);
      v_b = (int)(vfb * magn);
  }

  for (i = 0; i < 16; i++) {
      int dot = block[i*4+0]*v_r + block[i*4+1]*v_g + block[i*4+2]*v_b;
      if (dot < mind) { mind = dot; minp = block+i*4; }
      if (dot > maxd) { maxd = dot; maxp = block+i*4; }
  }

  *pmax16 = stb__As16Bit(maxp[0], maxp[1], maxp[2]);
  *pmin16 = stb__As16Bit(minp[0], minp[1], minp[2]);
}

int stb__sclamp(float y, int p0, int p1)
{
   int x = (int)y;
   if (x < p0) return p0;
   if (x > p1) return p1;
   return x;
}

int stb__RefineBlock(unsigned char *block, unsigned short *pmax16, unsigned short *pmin16, unsigned int mask)
{
   static const int w1Tab[4] = { 3,0,2,1 };
   static const int prods[4] = { 0x090000,0x000900,0x040102,0x010402 };
   float frb, fg;
   unsigned short oldMin = *pmin16, oldMax = *pmax16, min16, max16;
   int i, akku = 0, xx, xy, yy;
   int At1_r, At1_g, At1_b, At2_r, At2_g, At2_b;
   unsigned int cm = mask;

   if ((mask ^ (mask<<2)) < 4) {
      int r = 8, g = 8, b = 8;
      for (i = 0; i < 16; ++i) {
         r += block[i*4+0];
         g += block[i*4+1];
         b += block[i*4+2];
      }
      r >>= 4; g >>= 4; b >>= 4;
      max16 = (stb__OMatch5[r][0]<<11) | (stb__OMatch6[g][0]<<5) | stb__OMatch5[b][0];
      min16 = (stb__OMatch5[r][1]<<11) | (stb__OMatch6[g][1]<<5) | stb__OMatch5[b][1];
   } else {
      At1_r = At1_g = At1_b = 0;
      At2_r = At2_g = At2_b = 0;
      for (i = 0; i < 16; ++i, cm >>= 2) {
         int step = cm & 3;
         int w1 = w1Tab[step];
         int r = block[i*4+0];
         int g = block[i*4+1];
         int b = block[i*4+2];
         akku    += prods[step];
         At1_r   += w1*r;
         At1_g   += w1*g;
         At1_b   += w1*b;
         At2_r   += r;
         At2_g   += g;
         At2_b   += b;
      }
      At2_r = 3*At2_r - At1_r;
      At2_g = 3*At2_g - At1_g;
      At2_b = 3*At2_b - At1_b;

      xx = akku >> 16;
      yy = (akku >> 8) & 0xff;
      xy = (akku >> 0) & 0xff;

      frb = 3.0f * 31.0f / 255.0f / (xx*yy - xy*xy);
      fg = frb * 63.0f / 31.0f;

      max16 =   stb__sclamp((At1_r*yy - At2_r*xy)*frb+0.5f,0,31) << 11;
      max16 |=  stb__sclamp((At1_g*yy - At2_g*xy)*fg +0.5f,0,63) << 5;
      max16 |=  stb__sclamp((At1_b*yy - At2_b*xy)*frb+0.5f,0,31) << 0;

      min16 =   stb__sclamp((At2_r*xx - At1_r*xy)*frb+0.5f,0,31) << 11;
      min16 |=  stb__sclamp((At2_g*xx - At1_g*xy)*fg +0.5f,0,63) << 5;
      min16 |=  stb__sclamp((At2_b*xx - At1_b*xy)*frb+0.5f,0,31) << 0;
   }

   *pmin16 = min16;
   *pmax16 = max16;
   return (oldMin != min16) || (oldMax != max16);
}

void stb__CompressColorBlock(unsigned char *dest, unsigned char *block, int mode)
{
   unsigned int mask = 0;
   int i, dither = mode & STB_DXT_DITHER;
   int refinecount = (mode & STB_DXT_HIGHQUAL) ? 2 : 1;
   unsigned short max16, min16;
   unsigned char dblock[16*4], color[4*4];

   for (i = 1; i < 16; i++)
      if (((unsigned int *)block)[i] != ((unsigned int *)block)[0])
         break;

   if (i == 16) {
      int r = block[0], g = block[1], b = block[2];
      mask = 0xaaaaaaaa;
      max16 = (stb__OMatch5[r][0]<<11) | (stb__OMatch6[g][0]<<5) | stb__OMatch5[b][0];
      min16 = (stb__OMatch5[r][1]<<11) | (stb__OMatch6[g][1]<<5) | stb__OMatch5[b][1];
   } else {
      if (dither)
         stb__DitherBlock(dblock, block);

      stb__OptimizeColorsBlock(dither ? dblock : block, &max16, &min16);
      if (max16 != min16) {
         stb__EvalColors(color, max16, min16);
         mask = stb__MatchColorsBlock(block, color, dither);
      } else {
         mask = 0;
      }

      for (i = 0; i < refinecount; i++) {
         unsigned int lastmask = mask;
         if (stb__RefineBlock(dither ? dblock : block, &max16, &min16, mask)) {
            if (max16 != min16) {
               stb__EvalColors(color, max16, min16);
               mask = stb__MatchColorsBlock(block, color, dither);
            } else {
               mask = 0;
               break;
            }
         }
         if (mask == lastmask)
            break;
      }
   }

   if (max16 < min16) {
      unsigned short t = min16;
      min16 = max16;
      max16 = t;
      mask ^= 0x55555555;
   }

   dest[0] = (unsigned char)(max16);
   dest[1] = (unsigned char)(max16 >> 8);
   dest[2] = (unsigned char)(min16);
   dest[3] = (unsigned char)(min16 >> 8);
   dest[4] = (unsigned char)(mask);
   dest[5] = (unsigned char)(mask >> 8);
   dest[6] = (unsigned char)(mask >> 16);
   dest[7] = (unsigned char)(mask >> 24);
}

void stb__CompressAlphaBlock(unsigned char *dest, unsigned char *src, int stride)
{
   int i, dist, bias, dist4, dist2, bits, mask;
   int mn, mx;
   mn = mx = src[0];
   for (i = 1; i < 16; i++) {
      if (src[i*stride] < mn) mn = src[i*stride];
      else if (src[i*stride] > mx) mx = src[i*stride];
   }

   dest[0] = mx; dest[1] = mn; dest += 2;

   dist = mx - mn;
   dist4 = dist * 4;
   dist2 = dist * 2;
   bias = (dist < 8) ? (dist - 1) : (dist/2 + 2);
   bias -= mn * 7;
   bits = 0; mask = 0;

   for (i = 0; i < 16; i++) {
      int a = src[i*stride] * 7 + bias;
      int ind, t;
      t = (a >= dist4) ? -1 : 0; ind =  t & 4; a -= dist4 & t;
      t = (a >= dist2) ? -1 : 0; ind += t & 2; a -= dist2 & t;
      ind += (a >= dist);
      ind = -ind & 7;
      ind ^= (2 > ind);
      mask |= ind << bits;
      if ((bits += 3) >= 8) {
         *dest++ = (unsigned char)mask;
         mask >>= 8;
         bits -= 8;
      }
   }
}

void stb__InitDXT(void)
{
   int i;
   for (i = 0; i < 32; i++)
      stb__Expand5[i] = (unsigned char)((i<<3)|(i>>2));
   for (i = 0; i < 64; i++)
      stb__Expand6[i] = (unsigned char)((i<<2)|(i>>4));
   for (i = 0; i < 256+16; i++) {
      int v = i - 8;
      if (v < 0) v = 0; else if (v > 255) v = 255;
      stb__QuantRBTab[i] = stb__Expand5[stb__Mul8Bit(v,31)];
      stb__QuantGTab[i] = stb__Expand6[stb__Mul8Bit(v,63)];
   }
   stb__PrepareOptTable(&stb__OMatch5[0][0], stb__Expand5, 32);
   stb__PrepareOptTable(&stb__OMatch6[0][0], stb__Expand6, 64);
}

void stb_compress_dxt_block(unsigned char *dest, const unsigned char *src, int alpha, int mode)
{
   unsigned char data[16][4];
   static int init = 1;
   if (init) { stb__InitDXT(); init = 0; }

   if (alpha) {
      stb__CompressAlphaBlock(dest, (unsigned char*)src + 3, 4);
      dest += 8;
      memcpy(data, src, 4*16);
      for (int i = 0; i < 16; ++i)
         data[i][3] = 255;
      src = &data[0][0];
   }
   stb__CompressColorBlock(dest, (unsigned char*)src, mode);
}

void stb_compress_bc4_block(unsigned char *dest, const unsigned char *src)
{
   stb__CompressAlphaBlock(dest, (unsigned char*)src, 1);
}

void stb_compress_bc5_block(unsigned char *dest, const unsigned char *src)
{
   stb__CompressAlphaBlock(dest, (unsigned char*)src, 2);
   stb__CompressAlphaBlock(dest + 8, (unsigned char*)src + 1, 2);
}

int imin(int x, int y) { return (x < y) ? x : y; }

void extract_block(const unsigned char *src, int x, int y, int w, int h, unsigned char *block)
{
   int i, j;
   if ((w - x >= 4) && (h - y >= 4)) {
      src += x*4 + y*w*4;
      for (i = 0; i < 4; ++i) {
         *(unsigned int*)block = *(unsigned int*)src; block += 4; src += 4;
         *(unsigned int*)block = *(unsigned int*)src; block += 4; src += 4;
         *(unsigned int*)block = *(unsigned int*)src; block += 4; src += 4;
         *(unsigned int*)block = *(unsigned int*)src; block += 4;
         src += (w*4) - 12;
      }
      return;
   }
   int bw = imin(w - x, 4), bh = imin(h - y, 4);
   const int rem[] = { 0,0,0,0, 0,1,0,1, 0,1,2,0, 0,1,2,3 };
   for (i = 0; i < 4; ++i) {
      int by = rem[(bh - 1) * 4 + i] + y;
      for (j = 0; j < 4; ++j) {
         int bx = rem[(bw - 1) * 4 + j] + x;
         block[(i*16)+(j*4)+0] = src[(by*w*4)+(bx*4)+0];
         block[(i*16)+(j*4)+1] = src[(by*w*4)+(bx*4)+1];
         block[(i*16)+(j*4)+2] = src[(by*w*4)+(bx*4)+2];
         block[(i*16)+(j*4)+3] = src[(by*w*4)+(bx*4)+3];
      }
   }
}

uint64_t get_compress_pixels_dst_size_bytes(uint64_t w, uint64_t h, bool is_rgba)
{
   return is_rgba ? w * h : w * h / 2;
}

bool compress_pixels(unsigned char *dst, const unsigned char *src, uint64_t w, uint64_t h, bool is_rgba)
{
   if (w < 4 || w % 4 != 0 || h < 4 || h % 4 != 0)
      return false;

   unsigned char block[64];
   for (int y = 0; y < (int)h; y += 4) {
      for (int x = 0; x < (int)w; x += 4) {
         extract_block(src, x, y, (int)w, (int)h, block);
         stb_compress_dxt_block(dst, block, is_rgba ? 1 : 0, STB_DXT_HIGHQUAL);
         dst += is_rgba ? 16 : 8;
      }
   }
   return true;
}

// Декомпрессия DXT1 / DXT5 (BC1 / BC3)
static void decode_dxt1_block(const unsigned char *src, unsigned char *dst, int stride)
{
    unsigned short c0 = src[0] | (src[1] << 8);
    unsigned short c1 = src[2] | (src[3] << 8);
    unsigned int indices = (unsigned int)src[4] | ((unsigned int)src[5] << 8) |
                          ((unsigned int)src[6] << 16) | ((unsigned int)src[7] << 24);

    unsigned char col0[3], col1[3], col2[3], col3[3];
    col0[0] = ((c0 >> 11) & 0x1F) * 255 / 31;
    col0[1] = ((c0 >> 5) & 0x3F) * 255 / 63;
    col0[2] = (c0 & 0x1F) * 255 / 31;

    col1[0] = ((c1 >> 11) & 0x1F) * 255 / 31;
    col1[1] = ((c1 >> 5) & 0x3F) * 255 / 63;
    col1[2] = (c1 & 0x1F) * 255 / 31;

    if (c0 > c1) {
        col2[0] = (2*col0[0] + col1[0]) / 3;
        col2[1] = (2*col0[1] + col1[1]) / 3;
        col2[2] = (2*col0[2] + col1[2]) / 3;
        col3[0] = (col0[0] + 2*col1[0]) / 3;
        col3[1] = (col0[1] + 2*col1[1]) / 3;
        col3[2] = (col0[2] + 2*col1[2]) / 3;
    } else {
        col2[0] = (col0[0] + col1[0]) / 2;
        col2[1] = (col0[1] + col1[1]) / 2;
        col2[2] = (col0[2] + col1[2]) / 2;
        col3[0] = col3[1] = col3[2] = 0; 
    }

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            int idx = indices & 3;
            indices >>= 2;
            unsigned char *px = dst + (i * stride) + j * 4;
            switch (idx) {
                case 0: px[0] = col0[0]; px[1] = col0[1]; px[2] = col0[2]; break;
                case 1: px[0] = col1[0]; px[1] = col1[1]; px[2] = col1[2]; break;
                case 2: px[0] = col2[0]; px[1] = col2[1]; px[2] = col2[2]; break;
                case 3: px[0] = col3[0]; px[1] = col3[1]; px[2] = col3[2]; break;
            }
            px[3] = 255;
        }
    }
}

static void decode_dxt5_block(const unsigned char *src, unsigned char *dst, int stride)
{
    unsigned char alpha0 = src[0];
    unsigned char alpha1 = src[1];
    unsigned long long alpha_indices = 0;
    for (int i = 0; i < 6; ++i) {
        alpha_indices |= ((unsigned long long)src[2 + i]) << (i * 8);
    }

    decode_dxt1_block(src + 8, dst, stride);

    unsigned char alpha_palette[8];
    alpha_palette[0] = alpha0;
    alpha_palette[1] = alpha1;
    if (alpha0 > alpha1) {
        for (int i = 1; i <= 6; ++i) {
            alpha_palette[i+1] = (unsigned char)(((8 - i) * alpha0 + i * alpha1) / 8);
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            alpha_palette[i+1] = (unsigned char)(((6 - i) * alpha0 + i * alpha1) / 6);
        }
        alpha_palette[6] = 0;
        alpha_palette[7] = 255;
    }

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            int bit_offset = (i * 4 + j) * 3;
            int idx = (alpha_indices >> bit_offset) & 7;
            dst[(i * stride) + j * 4 + 3] = alpha_palette[idx];
        }
    }
}

// Python-обёртки
static PyObject* compress_dxt1(PyObject* self, PyObject* args)
{
    const unsigned char* src;
    Py_ssize_t src_len;
    int w, h;
    if (!PyArg_ParseTuple(args, "y#ii", &src, &src_len, &w, &h))
        return NULL;
    if (w < 4 || w % 4 != 0 || h < 4 || h % 4 != 0) {
        PyErr_SetString(PyExc_ValueError,
                        "Width and height must be multiples of 4 and >= 4");
        return NULL;
    }
    if (src_len < (Py_ssize_t)w * h * 3) {
        PyErr_SetString(PyExc_ValueError, "Source data too short for RGB");
        return NULL;
    }

    int rgba_size = w * h * 4;
    unsigned char* rgba = (unsigned char*)malloc(rgba_size);
    if (!rgba) return PyErr_NoMemory();

    for (int i = 0; i < w * h; ++i) {
        rgba[i*4 + 0] = src[i*3 + 0];
        rgba[i*4 + 1] = src[i*3 + 1];
        rgba[i*4 + 2] = src[i*3 + 2];
        rgba[i*4 + 3] = 255;
    }

    int blocks_x = w / 4;
    int blocks_y = h / 4;
    int dst_size = blocks_x * blocks_y * 8; 
    unsigned char* dst = (unsigned char*)malloc(dst_size);
    if (!dst) {
        free(rgba);
        return PyErr_NoMemory();
    }

    bool ok = compress_pixels(dst, rgba, w, h, false);
    free(rgba);

    if (!ok) {
        free(dst);
        PyErr_SetString(PyExc_RuntimeError, "Compression failed");
        return NULL;
    }

    PyObject* result = PyBytes_FromStringAndSize((const char*)dst, dst_size);
    free(dst);
    return result;
}

static PyObject* compress_dxt5(PyObject* self, PyObject* args)
{
    const unsigned char* src;
    Py_ssize_t src_len;
    int w, h;
    if (!PyArg_ParseTuple(args, "y#ii", &src, &src_len, &w, &h))
        return NULL;
    if (w < 4 || w % 4 != 0 || h < 4 || h % 4 != 0) {
        PyErr_SetString(PyExc_ValueError, "Width and height must be multiples of 4 and >= 4");
        return NULL;
    }
    if (src_len < (Py_ssize_t)w * h * 4) {
        PyErr_SetString(PyExc_ValueError, "Source data too short for RGBA");
        return NULL;
    }

    int blocks_x = w / 4, blocks_y = h / 4;
    int dst_size = blocks_x * blocks_y * 16;
    unsigned char* dst = (unsigned char*)malloc(dst_size);
    if (!dst) return PyErr_NoMemory();

    if (!compress_pixels(dst, src, w, h, true)) {
        free(dst);
        PyErr_SetString(PyExc_RuntimeError, "Compression failed");
        return NULL;
    }

    PyObject* result = PyBytes_FromStringAndSize((const char*)dst, dst_size);
    free(dst);
    return result;
}

static PyObject* decompress_dxt1(PyObject* self, PyObject* args)
{
    const unsigned char* src;
    Py_ssize_t src_len;
    int w, h;
    if (!PyArg_ParseTuple(args, "y#ii", &src, &src_len, &w, &h))
        return NULL;
    if (w < 4 || w % 4 != 0 || h < 4 || h % 4 != 0) {
        PyErr_SetString(PyExc_ValueError, "Width and height must be multiples of 4 and >= 4");
        return NULL;
    }
    int blocks_x = w / 4, blocks_y = h / 4;
    int expected_size = blocks_x * blocks_y * 8;
    if (src_len < expected_size) {
        PyErr_SetString(PyExc_ValueError, "Compressed data too short");
        return NULL;
    }

    int out_size = w * h * 3; 
    unsigned char* dst = (unsigned char*)malloc(out_size);
    if (!dst) return PyErr_NoMemory();

    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            int src_offset = (by * blocks_x + bx) * 8;
            unsigned char block[4*4*4]; 
            decode_dxt1_block(src + src_offset, block, 4*4);
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    int px = bx * 4 + j;
                    int py = by * 4 + i;
                    if (px < w && py < h) {
                        unsigned char* out = dst + (py * w + px) * 3;
                        unsigned char* in = block + (i * 4 + j) * 4;
                        out[0] = in[0];
                        out[1] = in[1];
                        out[2] = in[2];
                    }
                }
            }
        }
    }

    PyObject* result = PyBytes_FromStringAndSize((const char*)dst, out_size);
    free(dst);
    return result;
}

static PyObject* decompress_dxt5(PyObject* self, PyObject* args)
{
    const unsigned char* src;
    Py_ssize_t src_len;
    int w, h;
    if (!PyArg_ParseTuple(args, "y#ii", &src, &src_len, &w, &h))
        return NULL;
    if (w < 4 || w % 4 != 0 || h < 4 || h % 4 != 0) {
        PyErr_SetString(PyExc_ValueError, "Width and height must be multiples of 4 and >= 4");
        return NULL;
    }
    int blocks_x = w / 4, blocks_y = h / 4;
    int expected_size = blocks_x * blocks_y * 16;
    if (src_len < expected_size) {
        PyErr_SetString(PyExc_ValueError, "Compressed data too short");
        return NULL;
    }

    int out_size = w * h * 4; 
    unsigned char* dst = (unsigned char*)malloc(out_size);
    if (!dst) return PyErr_NoMemory();

    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            int src_offset = (by * blocks_x + bx) * 16;
            unsigned char block[4*4*4];
            decode_dxt5_block(src + src_offset, block, 4*4);
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    int px = bx * 4 + j;
                    int py = by * 4 + i;
                    if (px < w && py < h) {
                        unsigned char* out = dst + (py * w + px) * 4;
                        unsigned char* in = block + (i * 4 + j) * 4;
                        out[0] = in[0];
                        out[1] = in[1];
                        out[2] = in[2];
                        out[3] = in[3];
                    }
                }
            }
        }
    }

    PyObject* result = PyBytes_FromStringAndSize((const char*)dst, out_size);
    free(dst);
    return result;
}

static PyMethodDef CompressorMethods[] = {
    {"compress_dxt1", compress_dxt1, METH_VARARGS,
     "Compress RGB data to DXT1.\nArgs: src (bytes), width (int), height (int)\n"
     "Returns: compressed bytes (8 bytes per 4x4 block)"},
    {"compress_dxt5", compress_dxt5, METH_VARARGS,
     "Compress RGBA data to DXT5.\nArgs: src (bytes), width (int), height (int)\n"
     "Returns: compressed bytes (16 bytes per 4x4 block)"},
    {"decompress_dxt1", decompress_dxt1, METH_VARARGS,
     "Decompress DXT1 data to RGB.\nArgs: src (bytes), width (int), height (int)\n"
     "Returns: uncompressed RGB bytes (width*height*3)"},
    {"decompress_dxt5", decompress_dxt5, METH_VARARGS,
     "Decompress DXT5 data to RGBA.\nArgs: src (bytes), width (int), height (int)\n"
     "Returns: uncompressed RGBA bytes (width*height*4)"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef compressormodule = {
    PyModuleDef_HEAD_INIT,
    "_compressor",
    NULL,
    -1,
    CompressorMethods
};

PyMODINIT_FUNC PyInit__compressor(void)
{
    return PyModule_Create(&compressormodule);
}
