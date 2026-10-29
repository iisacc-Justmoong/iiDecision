"""Regenerate the checked-in Vulkan kernel with the official glslang tool."""
import pathlib
import os
import struct
import subprocess
import sys

app = pathlib.Path(__file__).resolve().parent
root = app.parents[2]
sdk = root.parent
for candidate in (sdk / "iiFileProvider/python", pathlib.Path(os.environ.get(
        "IIFILEPROVIDER_PREFIX", pathlib.Path.home() / ".local/SDK/iiFileProvider")) / "share/iiFileProvider/python"):
    if (candidate / "ii_file_provider/__init__.py").is_file():
        sys.path.insert(0, str(candidate))
        break
import ii_file_provider as provider

build = root / "build"
provider.mkdir(build, parents=True, exist_ok=True)
binary = build / "evidence.spv"
with provider.staged_path(binary, overwrite=True) as temporary:
    subprocess.run([sys.argv[1], "-V", "--target-env", "vulkan1.1", "-o", str(temporary),
                    str(app / "evidence.comp")], check=True)
    data = temporary.read_bytes()
words = struct.unpack("<" + "I" * (len(data) // 4), data)
lines = ["    " + ",".join(f"0x{w:08x}u" for w in words[i:i + 8]) + ","
         for i in range(0, len(words), 8)]
provider.write_text(app / "EvidenceShader.h",
    "// Generated from evidence.comp by App/compile_shader.py.\n#pragma once\n"
    "#include <cstdint>\nnamespace iiDecision::detail {\n"
    "inline constexpr std::uint32_t evidence_shader[]{\n" + "\n".join(lines) + "\n};\n}\n")
