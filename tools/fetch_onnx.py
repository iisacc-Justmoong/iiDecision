"""Fetch the free, pinned official ONNX Runtime C/C++ SDK into build/dependencies."""
import hashlib
import json
import pathlib
import platform
import tarfile
import urllib.request
import zipfile

version = "1.30.0"
root = pathlib.Path(__file__).resolve().parent.parent / "build/dependencies"
root.mkdir(parents=True, exist_ok=True)
systems = {"Darwin": "osx", "Linux": "linux", "Windows": "win"}
architectures = {"arm64": "arm64", "aarch64": "aarch64", "x86_64": "x64", "AMD64": "x64"}
system = systems[platform.system()]
arch = architectures[platform.machine()]
if system == "linux" and arch == "arm64":
    arch = "aarch64"
if system != "linux" and arch == "aarch64":
    arch = "arm64"
name = f"onnxruntime-{system}-{arch}-{version}"
extension = ".zip" if system == "win" else ".tgz"
url = f"https://github.com/microsoft/onnxruntime/releases/download/v{version}/{name}{extension}"
archive = root / (name + extension)
with urllib.request.urlopen(url) as response, archive.open("wb") as output:
    while chunk := response.read(1024 * 1024):
        output.write(chunk)
if extension == ".zip":
    with zipfile.ZipFile(archive) as package:
        for member in package.namelist():
            if not (root / member).resolve().is_relative_to(root.resolve()):
                raise ValueError("archive member escapes build directory")
        package.extractall(root)
else:
    with tarfile.open(archive) as package:
        package.extractall(root, filter="data")
(root / (name + ".json")).write_text(json.dumps({"url": url,
    "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}, indent=2) + "\n")
print(root / name)
