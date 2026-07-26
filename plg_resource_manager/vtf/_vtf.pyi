from typing import Any

def vtf_read(data: bytes) -> dict[str, Any]:
    """
    Parse a VTF file (v7.5) and return a dictionary with the following keys:
    - 'width': int
    - 'height': int
    - 'format': int
    - 'mip_count': int
    - 'frames': int
    - 'first_frame': int
    - 'reflectivity_r': float
    - 'reflectivity_g': float
    - 'reflectivity_b': float
    - 'bump_scale': float
    - 'low_res_format': int
    - 'low_res_width': int
    - 'low_res_height': int
    - 'flags': int
    - 'mipmaps': List[bytes]  # mip levels from smallest to largest (compressed)
    - 'thumbnail': Optional[bytes]  # DXT1 thumbnail data (or None)
    """
    ...

def vtf_write(
    width: int,
    height: int,
    format: int,
    flags: int,
    reflectivity_r: float,
    reflectivity_g: float,
    reflectivity_b: float,
    bump_scale: float,
    mipmaps: list[bytes],
    thumbnail: bytes | None,
) -> bytes:
    """
    Assemble a VTF file (v7.5) from pre-compressed mipmaps and thumbnail.

    Parameters:
    - width, height: dimensions of the largest mipmap.
    - format: VTF image format enum value (e.g., 13 for DXT1, 15 for DXT5).
    - flags: texture flags bitmask.
    - reflectivity_r/g/b: reflectivity vector components.
    - bump_scale: bumpmap scale factor.
    - mipmaps: list of compressed mip levels (bytes), ordered from smallest to largest.
    - thumbnail: compressed DXT1 thumbnail bytes, or None (will use a black placeholder).
    Returns the complete VTF file as bytes.
    """
    ...
