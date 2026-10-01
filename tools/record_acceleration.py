"""Record completed device tests and artifact hashes without treating discovery as execution."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys
from datetime import datetime, timezone

root = pathlib.Path(__file__).resolve().parent.parent
binary = pathlib.Path(sys.argv[1]).resolve()
output = pathlib.Path(sys.argv[2]).resolve()
commands = []
for arguments in (["Devices"], ["AllDevices"]):
    result = subprocess.run([str(binary), *arguments], capture_output=True, text=True, check=True)
    commands.append({"arguments": arguments, "exit_code": result.returncode,
                     "stdout": result.stdout, "stderr": result.stderr})
sources = sorted(p for folder in ("src", "tests", "tools", "cmake") for p in (root / folder).rglob("*")
                 if p.is_file() and "__pycache__" not in p.parts)
sources.append(root / "CMakeLists.txt")
library = pathlib.Path(sys.argv[3]).resolve()
artifacts = [binary, binary.parent / "iiDecisionOracleDriver", library]
report = {"created_utc": datetime.now(timezone.utc).isoformat(), "platform": platform.platform(),
          "commands": commands,
          "completed_backends": [line.split()[1] for line in commands[0]["stdout"].splitlines()
                                 if line.startswith("EXECUTED ")],
          "artifact_sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in artifacts if p.is_file()},
          "source_sha256": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n")
print("completed backends:", ", ".join(report["completed_backends"]))
