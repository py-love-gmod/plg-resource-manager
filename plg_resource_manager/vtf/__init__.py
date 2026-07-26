"""
VTF reader/writer (v7.5) для Garry's Mod.

Поддерживаемые форматы для записи:
- DXT1 (13)
- DXT5 (15)
- RGBA8888 (0)
- RGB888 (2)
- RGB565 (4)
- BGRA5551 (21)
- BGRA4444 (19)

Чтение работает для любого формата VTF.
"""

from __future__ import annotations

import struct
from enum import IntEnum, IntFlag

from PIL import Image

from ..dxt import compress_dxt1, compress_dxt5, decompress_dxt1, decompress_dxt5
from . import _vtf

# Форматы, доступные для записи
_WRITABLE_FORMATS = {
    0,  # RGBA8888
    2,  # RGB888
    4,  # RGB565
    19,  # BGRA4444
    21,  # BGRA5551
    13,  # DXT1
    15,  # DXT5
}


def _pack_rgba_to_format(rgba: bytes, width: int, height: int, fmt: int) -> bytes:
    if fmt == 0:  # RGBA8888
        return rgba

    elif fmt == 2:  # RGB888
        return bytes(b for i in range(0, len(rgba), 4) for b in rgba[i : i + 3])

    elif fmt == 4:  # RGB565
        out = bytearray()
        for i in range(0, len(rgba), 4):
            r, g, b = rgba[i], rgba[i + 1], rgba[i + 2]
            val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            out.extend(struct.pack("<H", val))

        return bytes(out)

    elif fmt == 19:  # BGRA4444
        out = bytearray()
        for i in range(0, len(rgba), 4):
            r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
            val = ((b >> 4) << 12) | ((g >> 4) << 8) | ((r >> 4) << 4) | (a >> 4)
            out.extend(struct.pack("<H", val))

        return bytes(out)

    elif fmt == 21:  # BGRA5551
        out = bytearray()
        for i in range(0, len(rgba), 4):
            r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
            val = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3) | ((a >> 7) & 1)
            out.extend(struct.pack("<H", val))

        return bytes(out)

    elif fmt == 13:  # DXT1
        rgb = bytes(b for i in range(0, len(rgba), 4) for b in rgba[i : i + 3])
        return compress_dxt1(rgb, width, height)

    elif fmt == 15:  # DXT5
        return compress_dxt5(rgba, width, height)

    raise ValueError(f"Unsupported format {fmt}")


def _unpack_format_to_rgba(data: bytes, width: int, height: int, fmt: int) -> bytes:
    if fmt == 0:  # RGBA8888
        return data

    elif fmt == 2:  # RGB888
        out = bytearray()
        for i in range(0, len(data), 3):
            out.extend([data[i], data[i + 1], data[i + 2], 255])

        return bytes(out)

    elif fmt == 4:  # RGB565
        out = bytearray()
        for i in range(0, len(data), 2):
            val = struct.unpack("<H", data[i : i + 2])[0]
            r = ((val >> 11) & 0x1F) << 3
            g = ((val >> 5) & 0x3F) << 2
            b = (val & 0x1F) << 3
            out.extend([r, g, b, 255])

        return bytes(out)

    elif fmt == 19:  # BGRA4444
        out = bytearray()
        for i in range(0, len(data), 2):
            val = struct.unpack("<H", data[i : i + 2])[0]
            b = ((val >> 12) & 0xF) << 4
            g = ((val >> 8) & 0xF) << 4
            r = ((val >> 4) & 0xF) << 4
            a = (val & 0xF) << 4
            out.extend([r, g, b, a])

        return bytes(out)

    elif fmt == 21:  # BGRA5551
        out = bytearray()
        for i in range(0, len(data), 2):
            val = struct.unpack("<H", data[i : i + 2])[0]
            b = ((val >> 10) & 0x1F) << 3
            g = ((val >> 5) & 0x1F) << 3
            r = (val & 0x1F) << 3
            a = 255 if (val & 0x8000) else 0
            out.extend([r, g, b, a])

        return bytes(out)

    elif fmt == 13:  # DXT1
        return decompress_dxt1(data, width, height)

    elif fmt == 15:  # DXT5
        return decompress_dxt5(data, width, height)

    return data


# Enum'ы
class ImageFormat(IntEnum):
    RGBA8888 = 0
    """32 бита на пиксель: 8 бит красный, 8 зелёный, 8 синий, 8 альфа (RGBA)."""

    ABGR8888 = 1
    """32 бита на пиксель: альфа, синий, зелёный, красный (ABGR)."""

    RGB888 = 2
    """24 бита на пиксель: красный, зелёный, синий. Без альфа-канала."""

    BGR888 = 3
    """24 бита на пиксель: синий, зелёный, красный. Без альфа-канала."""

    RGB565 = 4
    """16 бит на пиксель: 5 бит красный, 6 зелёный, 5 синий. Без альфа-канала."""

    I8 = 5
    """8 бит на пиксель: яркость (градации серого). Без альфа-канала."""

    IA88 = 6
    """16 бит на пиксель: 8 бит яркость, 8 бит альфа."""

    P8 = 7
    """8 бит на пиксель: палитра из 256 цветов. Не поддерживается в Garry's Mod."""

    A8 = 8
    """8 бит на пиксель: только альфа-канал. Цвет всегда белый."""

    RGB888_BLUESCREEN = 9
    """24 бита RGB, где чисто синий (#0000FF) интерпретируется как прозрачный."""

    BGR888_BLUESCREEN = 10
    """24 бита BGR, где чисто синий (#0000FF) интерпретируется как прозрачный."""

    ARGB8888 = 11
    """32 бита на пиксель: альфа, красный, зелёный, синий."""

    BGRA8888 = 12
    """32 бита на пиксель: синий, зелёный, красный, альфа."""

    DXT1 = 13
    """Сжатие DXT1 (BC1): 4 бита на пиксель. RGB + опциональная 1-битная альфа."""

    DXT3 = 14
    """Сжатие DXT3 (BC2): 8 бит на пиксель. RGB + 4-битная явная альфа (без интерполяции)."""

    DXT5 = 15
    """Сжатие DXT5 (BC3): 8 бит на пиксель. RGB + 8-битная интерполированная альфа."""

    BGRX8888 = 16
    """32 бита на пиксель: BGR, четвёртый байт игнорируется (альфа всегда 255)."""

    BGR565 = 17
    """16 бит на пиксель: синий 5, зелёный 6, красный 5."""

    BGRX5551 = 18
    """16 бит на пиксель: 5 бит синий, 5 зелёный, 5 красный, 1 бит не используется."""

    BGRA4444 = 19
    """16 бит на пиксель: по 4 бита на синий, зелёный, красный и альфа."""

    DXT1_ONEBITALPHA = 20
    """Сжатие DXT1 с поддержкой 1-битной альфы (аналогично обычному DXT1)."""

    BGRA5551 = 21
    """16 бит на пиксель: по 5 бит синий, зелёный, красный и 1 бит альфа (прозрачный/непрозрачный)."""

    UV88 = 22
    """16 бит на пиксель: 8 бит U, 8 бит V (карты смещений DuDv)."""

    UVWQ8888 = 23
    """32 бита на пиксель: 8 бит U, 8 V, 8 W, 8 Q."""

    RGBA16161616F = 24
    """64 бита на пиксель: HDR формат с 16-битными float-компонентами RGBA."""

    RGBA16161616 = 25
    """64 бита на пиксель: HDR формат с 16-битными целочисленными компонентами RGBA."""

    UVLX8888 = 26
    """32 бита на пиксель: дополнительный формат DuDv (U, V, L, X)."""


class TextureFlags(IntFlag):
    POINTSAMPLE = 0x00000001
    """Точечная фильтрация (без билинейной). Полезно для пиксель-арта. Внимание: ломает мип-уровни."""

    TRILINEAR = 0x00000002
    """Принудительная трилинейная фильтрация, даже если в настройках видео выбрано «Билинейная»."""

    CLAMPS = 0x00000004
    """Зажим текстурных координат по оси S (горизонталь). Предотвращает повторение (wrapping)."""

    CLAMPT = 0x00000008
    """Зажим текстурных координат по оси T (вертикаль). Предотвращает повторение."""

    ANISOTROPIC = 0x00000010
    """Принудительная анизотропная фильтрация, даже при других настройках качества."""

    HINT_DXT5 = 0x00000020
    """Подсказка движку: использовать алгоритм сжатия как для DXT5. Применяется для скайбоксов (убирает швы)."""

    NORMAL = 0x00000080
    """Текстура является картой нормалей."""

    NOMIP = 0x00000100
    """Не загружать мип-уровни. Загружается только самый большой уровень. Не удаляет уже существующие мипы."""

    NOLOD = 0x00000200
    """Не учитывать настройки уровня детализации (mat_picmip). Текстура всегда будет самой чёткой. Рекомендуется для HUD."""

    ALL_MIPS = 0x00000400
    """Загружать все мип-уровни, включая те, что меньше 32x32 (обычно они пропускаются). Полезно для размытых кубмап."""

    PROCEDURAL = 0x00000800
    """Текстура процедурная (движок может модифицировать её содержимое)."""

    ONEBITALPHA = 0x00001000
    """Текстура содержит однобитную альфу (только полностью прозрачный или непрозрачный). Обычно устанавливается автоматически."""

    EIGHTBITALPHA = 0x00002000
    """Текстура содержит 8-битную альфу (256 градаций прозрачности). Устанавливается автоматически для форматов с альфой."""

    ENVMAP = 0x00004000
    """Текстура является картой окружения (environment map)."""

    RENDERTARGET = 0x00008000
    """Текстура используется как цель рендеринга (render target)."""

    DEPTHRENDERTARGET = 0x00010000
    """Текстура является целью глубины (depth render target)."""

    NODEBUGOVERRIDE = 0x00020000
    """Запрещает отладочную замену текстуры (например, инструментами разработчика)."""

    SINGLECOPY = 0x00040000
    """Текстура должна храниться в единственном экземпляре (оптимизация памяти)."""

    PRE_SRGB = 0x00080000
    """К текстуре уже применена sRGB-коррекция цвета."""

    NODEPTHBUFFER = 0x00800000
    """Не создавать для этой текстуры буфер глубины. Используется для удалённых объектов, где буфер не нужен."""

    CLAMPU = 0x02000000
    """Зажим по оси U (для объёмных текстур)."""

    VERTEXTEXTURE = 0x04000000
    """Текстуру можно использовать как вершинную (vertex texture)."""

    SSBUMP = 0x08000000
    """Текстура является SSBump (карта самозатенения)."""

    BORDER = 0x20000000
    """При выходе за границы текстуры возвращать цвет рамки, а не повторять или зажимать к краю."""


# Чтение
def read_vtf(data: bytes, decompress: bool = True) -> dict:
    info = _vtf.vtf_read(data)
    fmt = info["format"]
    if decompress:
        top_mip = info["mipmaps"][-1]
        w, h = info["width"], info["height"]
        info["pixels"] = _unpack_format_to_rgba(top_mip, w, h, fmt)

    else:
        info["pixels"] = info["mipmaps"][-1]

    return info


def read_vtf_to_image(data: bytes) -> Image.Image:
    info = read_vtf(data, decompress=True)
    return Image.frombytes("RGBA", (info["width"], info["height"]), info["pixels"])


# Запись
def _generate_thumbnail(img: Image.Image) -> bytes:
    w, h = img.size
    while w > 16 or h > 16:
        w = max(1, w // 2)
        h = max(1, h // 2)

    thumb = img.resize((w, h), Image.Resampling.LANCZOS).convert("RGB")
    pad_w = ((w + 3) // 4) * 4
    pad_h = ((h + 3) // 4) * 4
    if pad_w != w or pad_h != h:
        padded = Image.new("RGB", (pad_w, pad_h), (0, 0, 0))
        padded.paste(thumb, (0, 0))
        thumb = padded

    return compress_dxt1(thumb.tobytes(), thumb.width, thumb.height)


def write_vtf(
    pixels: bytes,
    width: int,
    height: int,
    format: ImageFormat | int = ImageFormat.DXT5,
    mipmaps: bool = True,
    flags: TextureFlags | None = None,
    reflectivity: tuple[float, float, float] = (0.5, 0.5, 0.5),
    bump_scale: float = 1.0,
) -> bytes:
    if flags is None:
        flags = TextureFlags(0)

    fmt = int(format)
    if fmt not in _WRITABLE_FORMATS:
        raise ValueError(f"Format {fmt} not supported for writing")

    img = Image.frombytes("RGBA", (width, height), pixels)

    # Mipmaps
    mip_list = []
    if mipmaps:
        cur_w, cur_h = width, height
        cur_img = img
        big_to_small = []
        while True:
            data = _pack_rgba_to_format(cur_img.tobytes(), cur_w, cur_h, fmt)
            big_to_small.append(data)
            new_w = max(1, cur_w // 2)
            new_h = max(1, cur_h // 2)
            if new_w == cur_w and new_h == cur_h:
                break

            cur_img = cur_img.resize((new_w, new_h), Image.Resampling.LANCZOS)
            cur_w, cur_h = new_w, new_h

        mip_list = list(reversed(big_to_small))

    else:
        data = _pack_rgba_to_format(pixels, width, height, fmt)
        mip_list = [data]

    thumb = _generate_thumbnail(img)

    flags_int = flags.value
    if fmt == 15:  # DXT5
        flags_int |= TextureFlags.EIGHTBITALPHA

    elif fmt == 13:  # DXT1
        flags_int |= TextureFlags.ONEBITALPHA

    elif fmt in (19, 21):  # BGRA4444, BGRA5551 - есть альфа
        flags_int |= TextureFlags.EIGHTBITALPHA

    return _vtf.vtf_write(
        width,
        height,
        fmt,
        flags_int,
        reflectivity[0],
        reflectivity[1],
        reflectivity[2],
        bump_scale,
        mip_list,
        thumb,
    )


def write_vtf_from_image(
    image: Image.Image,
    format: ImageFormat | int = ImageFormat.DXT5,
    mipmaps: bool = True,
    flags: TextureFlags | None = None,
    reflectivity: tuple[float, float, float] = (0.5, 0.5, 0.5),
    bump_scale: float = 1.0,
) -> bytes:
    if image.mode != "RGBA":
        image = image.convert("RGBA")

    return write_vtf(
        image.tobytes(),
        image.width,
        image.height,
        format,
        mipmaps,
        flags,
        reflectivity,
        bump_scale,
    )
