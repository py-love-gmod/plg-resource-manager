import pytest

from plg_resource_manager.dxt import (
    compress_dxt1,
    compress_dxt5,
    decompress_dxt1,
    decompress_dxt5,
)


# helpers
def rgb_solid(w: int, h: int, r: int, g: int, b: int) -> bytes:
    return bytes([r, g, b]) * (w * h)


def rgba_solid(w: int, h: int, r: int, g: int, b: int, a: int) -> bytes:
    return bytes([r, g, b, a]) * (w * h)


def _max_abs_diff(a: bytes, b: bytes) -> int:
    return max((abs(x - y) for x, y in zip(a, b)), default=0)


def _avg_abs_diff(a: bytes, b: bytes) -> float:
    diffs = [abs(x - y) for x, y in zip(a, b)]
    return sum(diffs) / len(diffs) if diffs else 0.0


# size checks
def test_dxt1_output_size():
    w, h = 4, 4
    data = rgb_solid(w, h, 128, 64, 32)
    compressed = compress_dxt1(data, w, h)
    assert len(compressed) == 8


def test_dxt5_output_size():
    w, h = 4, 4
    data = rgba_solid(w, h, 255, 0, 0, 128)
    compressed = compress_dxt5(data, w, h)
    assert len(compressed) == 16


@pytest.mark.parametrize("w,h", [(8, 4), (4, 8), (12, 16)])
def test_dxt1_output_size_multiple_blocks(w, h):
    data = rgb_solid(w, h, 200, 100, 50)
    compressed = compress_dxt1(data, w, h)
    blocks = (w // 4) * (h // 4)
    assert len(compressed) == blocks * 8


# roundtrip - solid colors
def test_dxt1_solid_red_roundtrip():
    w, h = 4, 4
    orig = rgb_solid(w, h, 255, 0, 0)
    comp = compress_dxt1(orig, w, h)
    decomp = decompress_dxt1(comp, w, h)
    pixels = [decomp[i : i + 3] for i in range(0, len(decomp), 3)]
    assert all(p == pixels[0] for p in pixels), "Solid block became non-uniform"
    assert _max_abs_diff(orig, decomp) <= 10


def test_dxt5_solid_red_alpha_roundtrip():
    w, h = 4, 4
    orig = rgba_solid(w, h, 255, 0, 0, 128)
    comp = compress_dxt5(orig, w, h)
    decomp = decompress_dxt5(comp, w, h)
    pixels = [decomp[i : i + 4] for i in range(0, len(decomp), 4)]
    assert all(p == pixels[0] for p in pixels), "Solid block became non-uniform"
    assert _max_abs_diff(orig, decomp) <= 15


# roundtrip - gradients (visual quality)
def test_dxt1_gradient_roundtrip():
    w, h = 64, 64
    orig = bytearray()
    for y in range(h):
        for x in range(w):
            orig.extend([x % 256, y % 256, (x + y) % 256])

    orig = bytes(orig)
    comp = compress_dxt1(orig, w, h)
    decomp = decompress_dxt1(comp, w, h)
    assert _avg_abs_diff(orig, decomp) < 30


def test_dxt5_gradient_roundtrip():
    w, h = 64, 64
    orig = bytearray()
    for y in range(h):
        for x in range(w):
            orig.extend([x % 256, y % 256, (x + y) % 256, (x * 2) % 256])
    orig = bytes(orig)
    comp = compress_dxt5(orig, w, h)
    decomp = decompress_dxt5(comp, w, h)
    assert _avg_abs_diff(orig, decomp) < 40


# block structure validation (basic)
def test_dxt1_black_block_roundtrip():
    w, h = 4, 4
    data = rgb_solid(w, h, 0, 0, 0)
    comp = compress_dxt1(data, w, h)
    decomp = decompress_dxt1(comp, w, h)
    assert decomp == b"\x00" * (w * h * 3)


def test_dxt5_block_alpha_order():
    w, h = 4, 4
    data = rgba_solid(w, h, 255, 0, 0, 128)
    comp = compress_dxt5(data, w, h)
    a0, a1 = comp[0], comp[1]
    assert 0 <= a0 <= 255
    assert 0 <= a1 <= 255
    assert abs(a0 - 128) < 20 or abs(a1 - 128) < 20


# decompression of known-good blocks (specification samples)
def test_decompress_dxt1_reference_block():
    block = bytes([0x00, 0xF8, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00])
    decomp = decompress_dxt1(block, 4, 4)
    for i in range(0, len(decomp), 3):
        r, g, b = decomp[i], decomp[i + 1], decomp[i + 2]
        assert 245 <= r <= 255
        assert g <= 8
        assert b <= 8


def test_decompress_dxt5_reference_block():
    alpha = bytes([128, 128, 0, 0, 0, 0, 0, 0])
    color = bytes([0x00, 0xF8, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00])
    block = alpha + color
    decomp = decompress_dxt5(block, 4, 4)
    for i in range(0, len(decomp), 4):
        r, g, b, a = decomp[i], decomp[i + 1], decomp[i + 2], decomp[i + 3]
        assert 245 <= r <= 255
        assert g <= 8
        assert b <= 8
        assert 120 <= a <= 136


# error handling / invalid inputs
def test_compress_dxt1_invalid_size():
    with pytest.raises(ValueError, match="must be multiples of 4"):
        compress_dxt1(b"\x00" * 12, 3, 4)


def test_compress_dxt5_invalid_size():
    with pytest.raises(ValueError, match="must be multiples of 4"):
        compress_dxt5(b"\x00" * 16, 4, 3)


def test_compress_dxt1_short_data():
    with pytest.raises(ValueError, match="too short"):
        compress_dxt1(b"\x00" * 10, 4, 4)


def test_compress_dxt5_short_data():
    with pytest.raises(ValueError, match="too short"):
        compress_dxt5(b"\x00" * 10, 4, 4)


def test_decompress_dxt1_short_data():
    with pytest.raises(ValueError, match="too short"):
        decompress_dxt1(b"\x00" * 4, 4, 4)


def test_decompress_dxt5_short_data():
    with pytest.raises(ValueError, match="too short"):
        decompress_dxt5(b"\x00" * 10, 4, 4)


# roundtrip random (smoke test - only check uniformity)
def test_dxt1_random_block_uniform():
    w, h = 4, 4
    for _ in range(20):
        r, g, b = 123, 45, 67
        data = rgb_solid(w, h, r, g, b)
        comp = compress_dxt1(data, w, h)
        decomp = decompress_dxt1(comp, w, h)
        pixels = [decomp[i : i + 3] for i in range(0, len(decomp), 3)]
        assert all(p == pixels[0] for p in pixels)


def test_dxt5_random_block_uniform():
    w, h = 4, 4
    for _ in range(20):
        r, g, b, a = 200, 100, 50, 180
        data = rgba_solid(w, h, r, g, b, a)
        comp = compress_dxt5(data, w, h)
        decomp = decompress_dxt5(comp, w, h)
        pixels = [decomp[i : i + 4] for i in range(0, len(decomp), 4)]
        assert all(p == pixels[0] for p in pixels)


@pytest.mark.parametrize(
    "color",
    [
        (255, 255, 255),  # белый
        (0, 0, 0),  # чёрный
        (255, 0, 255),  # пурпурный
        (0, 255, 255),  # циан
    ],
)
def test_dxt1_extreme_colors(color):
    w, h = 4, 4
    data = rgb_solid(w, h, *color)
    comp = compress_dxt1(data, w, h)
    decomp = decompress_dxt1(comp, w, h)
    for i in range(0, len(decomp), 3):
        for c in decomp[i : i + 3]:
            assert 0 <= c <= 255
