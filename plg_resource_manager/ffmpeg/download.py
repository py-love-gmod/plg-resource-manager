import os
import platform
import shutil
import stat
import tarfile
import urllib.request
import zipfile
from pathlib import Path

BIN_DIR = Path.home() / ".plg" / "bin"

PLATFORM_MAP = {
    ("linux", "x86_64"): "ffmpeg-master-latest-linux64-gpl.tar.xz",
    ("linux", "aarch64"): "ffmpeg-master-latest-linuxarm64-gpl.tar.xz",
    ("windows", "x86_64"): "ffmpeg-master-latest-win64-gpl.zip",
    ("windows", "arm64"): "ffmpeg-master-latest-winarm64-gpl.zip",
    ("darwin", "x86_64"): "ffmpeg-master-latest-macos64-gpl.tar.xz",
    ("darwin", "arm64"): "ffmpeg-master-latest-macosarm64-gpl.tar.xz",
}


def _get_platform_key() -> tuple[str, str]:
    system = platform.system().lower()
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        machine = "x86_64"

    elif machine in ("aarch64", "arm64"):
        machine = "arm64"

    return system, machine


def _get_ffmpeg_filename() -> str:
    key = _get_platform_key()
    filename = PLATFORM_MAP.get(key)
    if not filename:
        raise RuntimeError(
            f"Unsupported platform: {key[0]}/{key[1]}. "
            f"Supported: {', '.join(f'{s}/{m}' for (s, m) in PLATFORM_MAP)}"
        )

    return filename


def download_ffmpeg() -> Path:
    """
    Скачивает и устанавливает ffmpeg в BIN_DIR.
    Возвращает путь к исполняемому файлу.
    """
    BIN_DIR.mkdir(parents=True, exist_ok=True)

    is_windows = platform.system() == "Windows"
    ffmpeg_exe = BIN_DIR / ("ffmpeg.exe" if is_windows else "ffmpeg")

    if ffmpeg_exe.exists() and ffmpeg_exe.is_file():
        return ffmpeg_exe

    filename = _get_ffmpeg_filename()
    url = f"https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/{filename}"
    archive_path = (
        BIN_DIR / f"ffmpeg-{_get_platform_key()[0]}-{_get_platform_key()[1]}.tmp"
    )
    urllib.request.urlretrieve(url, archive_path)

    if filename.endswith(".tar.xz"):
        with tarfile.open(archive_path, "r:xz") as tf:
            tf.extractall(BIN_DIR)

    elif filename.endswith(".zip"):
        with zipfile.ZipFile(archive_path, "r") as zf:
            zf.extractall(BIN_DIR)

    else:
        raise RuntimeError(f"Unsupported archive format: {filename}")

    extracted_ffmpeg = None
    for root, _, files in os.walk(BIN_DIR):
        for f in files:
            if f == "ffmpeg" or f == "ffmpeg.exe":
                extracted_ffmpeg = Path(root) / f
                break

        if extracted_ffmpeg:
            break

    if not extracted_ffmpeg:
        raise RuntimeError("Failed to find ffmpeg executable in extracted archive")

    shutil.move(str(extracted_ffmpeg), str(ffmpeg_exe))
    if not is_windows:
        ffmpeg_exe.chmod(ffmpeg_exe.stat().st_mode | stat.S_IEXEC)

    archive_path.unlink(missing_ok=True)
    for item in BIN_DIR.iterdir():
        if item.is_dir() and item.name.startswith("ffmpeg"):
            shutil.rmtree(item, ignore_errors=True)

    return ffmpeg_exe
