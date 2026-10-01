"""Regenerate the checked-in Vulkan kernel with the official glslang tool."""
import pathlib
import struct
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent
build = root / "build"
build.mkdir(exist_ok=True)
binary = build / "evidence.spv"
subprocess.run([sys.argv[1], "-V", "--target-env", "vulkan1.1", "-o", str(binary),
                str(root / "src/backends/evidence.comp")], check=True)
data = binary.read_bytes()
words = struct.unpack("<" + "I" * (len(data) // 4), data)
lines = ["    " + ",".join(f"0x{w:08x}u" for w in words[i:i + 8]) + ","
         for i in range(0, len(words), 8)]
(root / "src/backends/EvidenceShader.h").write_text(
    "// Generated from evidence.comp by tools/compile_shader.py.\n#pragma once\n"
    "#include <cstdint>\nnamespace iiDecision::detail {\n"
    "inline constexpr std::uint32_t evidence_shader[]{\n" + "\n".join(lines) + "\n};\n}\n")
