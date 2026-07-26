#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define VTF_MAJOR 7
#define VTF_MINOR 5

static const unsigned char RES_TAG_THUMBNAIL[3] = {0x01, 0, 0};
static const unsigned char RES_TAG_IMAGE[3]     = {0x30, 0, 0};

static inline int pad4(int x) { return ((x + 3) / 4) * 4; }

static PyObject* vtf_read(PyObject* self, PyObject* args) {
    const unsigned char *data;
    Py_ssize_t len;
    if (!PyArg_ParseTuple(args, "y#", &data, &len))
        return NULL;

    if (len < 80) {
        PyErr_SetString(PyExc_ValueError, "Data too short for VTF header");
        return NULL;
    }
    if (memcmp(data, "VTF\0", 4) != 0) {
        PyErr_SetString(PyExc_ValueError, "Not a valid VTF file");
        return NULL;
    }

    const unsigned char *p = data + 4;
    uint32_t ver_major = *(uint32_t*)p; p += 4;
    uint32_t ver_minor = *(uint32_t*)p; p += 4;
    if (ver_major != 7) {
        PyErr_SetString(PyExc_ValueError, "Unsupported VTF version");
        return NULL;
    }
    uint32_t header_size = *(uint32_t*)p; p += 4;
    uint16_t width  = *(uint16_t*)p; p += 2;
    uint16_t height = *(uint16_t*)p; p += 2;
    uint32_t flags  = *(uint32_t*)p; p += 4;
    uint16_t frames = *(uint16_t*)p; p += 2;
    uint16_t first_frame = *(uint16_t*)p; p += 2;
    p += 4;
    float refl_r = *(float*)p; p += 4;
    float refl_g = *(float*)p; p += 4;
    float refl_b = *(float*)p; p += 4;
    p += 4; 
    float bump_scale = *(float*)p; p += 4;
    int32_t high_fmt = *(int32_t*)p; p += 4;
    uint8_t mip_count = *p; p++;
    int32_t low_fmt  = *(int32_t*)p; p += 4;
    uint8_t low_w = *p; p++;
    uint8_t low_h = *p; p++;
    uint16_t depth  = *(uint16_t*)p; p += 2;
    p += 3;
    uint32_t num_res = *(uint32_t*)p; p += 4;
    p += 8; 

    uint32_t thumb_off = 0, thumb_size = 0, image_off = 0;
    int has_thumb = 0;
    const unsigned char *res_ptr = data + 80;
    for (uint32_t i = 0; i < num_res; i++) {
        uint32_t off = *(uint32_t*)(res_ptr + 4);
        if (memcmp(res_ptr, RES_TAG_THUMBNAIL, 3) == 0) {
            thumb_off = off;
            has_thumb = 1;
        } else if (memcmp(res_ptr, RES_TAG_IMAGE, 3) == 0) {
            image_off = off;
        }
        res_ptr += 8;
    }
    if (has_thumb && image_off) {
        thumb_size = image_off - thumb_off;
    }

    int block_size = 0;
    if (high_fmt == 13 || high_fmt == 20)
        block_size = 8;
    else if (high_fmt == 15)
        block_size = 16;
    else {
        PyErr_SetString(PyExc_ValueError,
            "Only DXT1/DXT5 high-res formats are supported for mip extraction");
        return NULL;
    }

    int cur_w = width, cur_h = height;
    uint32_t off = image_off;
    PyObject *mip_list = PyList_New(0);
    for (int i = 0; i < mip_count; i++) {
        int pw = pad4(cur_w);
        int ph = pad4(cur_h);
        int data_sz = (pw / 4) * (ph / 4) * block_size;
        if (off + data_sz > len) {
            PyErr_SetString(PyExc_ValueError, "Corrupt VTF: image data out of bounds");
            Py_DECREF(mip_list);
            return NULL;
        }
        PyObject *mip_bytes = PyBytes_FromStringAndSize(
                                    (const char*)(data + off), data_sz);
        PyList_Append(mip_list, mip_bytes);
        Py_DECREF(mip_bytes);
        off += data_sz;
        cur_w = cur_w > 1 ? cur_w / 2 : 1;
        cur_h = cur_h > 1 ? cur_h / 2 : 1;
    }

    PyObject *thumb_obj = Py_None;
    if (has_thumb && thumb_size) {
        thumb_obj = PyBytes_FromStringAndSize(
                        (const char*)(data + thumb_off), thumb_size);
    } else {
        Py_INCREF(Py_None);
    }

    return Py_BuildValue("{s:i s:i s:i s:i s:i s:i s:f s:f s:f s:f s:i s:i s:i s:i s:O s:O}",
        "width",           width,
        "height",          height,
        "format",          high_fmt,
        "mip_count",       mip_count,
        "frames",          frames,
        "first_frame",     first_frame,
        "reflectivity_r",  refl_r,
        "reflectivity_g",  refl_g,
        "reflectivity_b",  refl_b,
        "bump_scale",      bump_scale,
        "low_res_format",  low_fmt,
        "low_res_width",   low_w,
        "low_res_height",  low_h,
        "flags",           flags,
        "mipmaps",         mip_list,
        "thumbnail",       thumb_obj
    );
}

static PyObject* vtf_write(PyObject* self, PyObject* args) {
    int width, height, format, flags;
    float refl_r, refl_g, refl_b, bump_scale;
    PyObject *mip_list;     
    PyObject *thumb_bytes;    

    if (!PyArg_ParseTuple(args, "iiiiiffffOO",
                          &width, &height, &format, &flags,
                          &refl_r, &refl_g, &refl_b, &bump_scale,
                          &mip_list, &thumb_bytes))
        return NULL;

    if (format != 13 && format != 15) { 
        PyErr_SetString(PyExc_ValueError, "Only DXT1 (13) and DXT5 (15) supported");
        return NULL;
    }
    if (!PyList_Check(mip_list)) {
        PyErr_SetString(PyExc_TypeError, "mipmaps must be a list of bytes");
        return NULL;
    }
    Py_ssize_t mip_count = PyList_Size(mip_list);
    if (mip_count == 0) {
        PyErr_SetString(PyExc_ValueError, "At least one mipmap required");
        return NULL;
    }

    const unsigned char *thumb_ptr = NULL;
    int thumb_size = 0;
    unsigned char dummy_thumb[8] = {0};  

    if (thumb_bytes != Py_None) {
        if (!PyBytes_Check(thumb_bytes)) {
            PyErr_SetString(PyExc_TypeError, "thumbnail must be bytes or None");
            return NULL;
        }
        thumb_size = (int)PyBytes_Size(thumb_bytes);
        thumb_ptr  = (const unsigned char*)PyBytes_AsString(thumb_bytes);
    } else {
        thumb_size = 8;
        thumb_ptr  = dummy_thumb;
    }

    int total_mip_size = 0;
    for (Py_ssize_t i = 0; i < mip_count; i++) {
        PyObject *item = PyList_GetItem(mip_list, i);  
        if (!PyBytes_Check(item)) {
            PyErr_SetString(PyExc_TypeError, "Each mipmap must be bytes");
            return NULL;
        }
        total_mip_size += (int)PyBytes_Size(item);
    }

    int num_resources = 2;
    int header_base    = 80;
    int res_entry_size = 8;
    int header_size    = header_base + num_resources * res_entry_size;
    int thumb_offset   = header_size;
    int image_offset   = header_size + thumb_size;

    int low_w = width, low_h = height;
    while (low_w > 16 || low_h > 16) {
        low_w = low_w > 1 ? low_w / 2 : 1;
        low_h = low_h > 1 ? low_h / 2 : 1;
    }

    unsigned char header[80];
    memset(header, 0, sizeof(header));
    memcpy(header, "VTF\0", 4);
    *(uint32_t*)(header + 4)  = VTF_MAJOR;
    *(uint32_t*)(header + 8)  = VTF_MINOR;
    *(uint32_t*)(header + 12) = header_size;
    *(uint16_t*)(header + 16) = (uint16_t)width;
    *(uint16_t*)(header + 18) = (uint16_t)height;
    *(uint32_t*)(header + 20) = flags;
    *(uint16_t*)(header + 24) = 1;        
    *(uint16_t*)(header + 26) = 0;    
    *(float*)(header + 32)    = refl_r;
    *(float*)(header + 36)    = refl_g;
    *(float*)(header + 40)    = refl_b;
    *(float*)(header + 48)    = bump_scale;
    *(int32_t*)(header + 52)  = format;
    header[56]                = (unsigned char)mip_count;
    *(int32_t*)(header + 57)  = 13;   
    header[61]                = (unsigned char)low_w;
    header[62]                = (unsigned char)low_h;
    *(uint16_t*)(header + 63) = 1;
    *(uint32_t*)(header + 68) = num_resources;

    unsigned char res[16];
    memcpy(res,     RES_TAG_THUMBNAIL, 3);
    res[3] = 0x00;
    *(uint32_t*)(res + 4) = thumb_offset;
    memcpy(res + 8, RES_TAG_IMAGE, 3);
    res[11] = 0x00;
    *(uint32_t*)(res + 12) = image_offset;

    int total_size = header_size + 16 + thumb_size + total_mip_size;
    unsigned char *out = (unsigned char*)malloc(total_size);
    if (!out) return PyErr_NoMemory();

    unsigned char *dst = out;
    memcpy(dst, header, 80); dst += 80;
    memcpy(dst, res,    16); dst += 16;
    memcpy(dst, thumb_ptr, thumb_size); dst += thumb_size;

    for (Py_ssize_t i = 0; i < mip_count; i++) {
        PyObject *item = PyList_GetItem(mip_list, i);
        int sz = (int)PyBytes_Size(item);
        memcpy(dst, PyBytes_AsString(item), sz);
        dst += sz;
    }

    PyObject *result = PyBytes_FromStringAndSize((const char*)out, total_size);
    free(out);
    return result;
}

// Module definition
static PyMethodDef VtfMethods[] = {
    {"vtf_read",  vtf_read,  METH_VARARGS, "Parse VTF file, return dict with header, mipmaps, thumbnail."},
    {"vtf_write", vtf_write, METH_VARARGS, "Assemble VTF file from pre-compressed mipmaps and thumbnail."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef vtfmodule = {
    PyModuleDef_HEAD_INIT,
    "_vtf",
    NULL,
    -1,
    VtfMethods
};

PyMODINIT_FUNC PyInit__vtf(void) {
    return PyModule_Create(&vtfmodule);
}
