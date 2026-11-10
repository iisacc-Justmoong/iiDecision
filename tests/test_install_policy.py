"""Exercise the installer plan with fake tools and no installation or device access."""
import os
import re
from pathlib import Path
import subprocess
import sys
import ii_file_provider as provider

project = Path(__file__).resolve().parents[1]
owner = Path(sys.argv[1]).resolve()
bin_dir = owner / "bin"
provider.mkdir(bin_dir, exist_ok=False)
for name in ("cmake", "ctest"):
    path = bin_dir / name
    provider.write_text(path, "#!/bin/sh\nprintf '" + name + ":'\nprintf ' <%s>' \"$@\"\nprintf '\\n'\n")
    path.chmod(0o700)
env = dict(os.environ, PATH=str(bin_dir) + os.pathsep + "/usr/bin:/bin",
           IIDECISION_INSTALL_PREFIX=str(owner / "install"))
env.pop("IIDECISION_VALIDATE_DEVICES", None)
def run():
    result = subprocess.run(["/bin/bash", str(project / "install.sh")], env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout
output = run()
assert str(project / "build") in output and "<--install>" in output
assert "<-DCMAKE_INSTALL_PREFIX=" + env["IIDECISION_INSTALL_PREFIX"] + ">" in output
assert "<-E>" in output and "(mlx|opencl)" in output
ctest_line = next(line for line in output.splitlines() if line.startswith("ctest:"))
pattern = ctest_line.split(" <-E> <", 1)[1].removesuffix(">")
assert re.fullmatch(pattern, "iiDecision.Acceleration.opencl")
assert re.fullmatch(pattern, "iiDecision.Acceleration.mlx")
assert not re.fullmatch(pattern, "iiDecision.Acceleration.Scalar")
env["IIDECISION_VALIDATE_DEVICES"] = "1"
output = run()
assert "<--test-dir>" in output and "<-E>" not in output
