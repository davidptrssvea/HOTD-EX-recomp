"""Package the tracked, asset-free HOTD EX builder for a release tag."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[1]
BUNDLE = "HOTDEX-Local-Builder"
EXCLUDED = {".exe", ".dll", ".obj", ".o", ".elf", ".iso", ".bin", ".jpg", ".sfd",
            ".adx", ".wav", ".mp3", ".dds", ".zip", ".gz", ".gzp", ".pyc"}


def package(tag, destination):
    version = (ROOT / "ui/hotd-update-version.txt").read_text(encoding="ascii").strip()
    if not re.fullmatch(r"v\d+\.\d+\.\d+", tag) or version != tag:
        raise ValueError("The release tag must match the installed version stamp.")
    manifest = (ROOT / "package-manifest.json").read_bytes()
    contents = {"package-manifest.json": manifest}
    for entry in json.loads(manifest)["files"]:
        name = entry["path"]
        path = PurePosixPath(name)
        if (path.is_absolute() or path.as_posix() != name or "\\" in name or ":" in name
                or name in contents or set(path.parts) & {"..", "generated", "build", "cache", ".git"}
                or path.suffix.lower() in EXCLUDED):
            raise ValueError(f"Invalid package entry: {name}")
        data = (ROOT / name).read_bytes()
        if data.startswith((b"MZ", b"\x7fELF")) or hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"Unverified package entry: {name}")
        contents[name] = data

    destination.mkdir(parents=True, exist_ok=True)
    archive = destination / f"{BUNDLE}-{tag}.zip"
    with ZipFile(archive, "w") as output:
        for name, data in sorted(contents.items()):
            info = ZipInfo(f"{BUNDLE}/{name}", (2026, 11, 17, 0, 0, 0))
            info.external_attr = 0o100644 << 16
            output.writestr(info, data, compress_type=ZIP_DEFLATED, compresslevel=9)
    with ZipFile(archive) as output:
        if output.testzip() is not None:
            raise ValueError("The generated archive failed its integrity check.")
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256").write_bytes(f"{checksum}  {archive.name}\n".encode("ascii"))
    return archive


if __name__ == "__main__":
    print(package(sys.argv[1], Path(sys.argv[2])))
