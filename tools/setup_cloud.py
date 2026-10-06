#!/usr/bin/env python3
"""Reproduce the verified cloud Java/Gradle workflow, outside other checkouts."""
import hashlib
import pathlib
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

root = pathlib.Path(__file__).resolve().parents[1]
toolchains = pathlib.Path("/workspace/toolchains")
jdks = sorted(p for p in toolchains.glob("jdk-21*") if (p / "bin/javac").is_file())
if not jdks:
    toolchains.mkdir(parents=True, exist_ok=True)
    url = "https://download.oracle.com/java/21/latest/jdk-21_linux-x64_bin.tar.gz"
    expected = urllib.request.urlopen(url + ".sha256", timeout=30).read().decode().strip()
    if len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
        sys.exit("Invalid publisher JDK checksum")
    with tempfile.TemporaryDirectory(prefix="uebridge-jdk-") as temporary:
        archive = pathlib.Path(temporary) / "jdk.tar.gz"
        with urllib.request.urlopen(url, timeout=180) as response, archive.open("wb") as output:
            while chunk := response.read(1024 * 1024): output.write(chunk)
        digest = hashlib.file_digest(archive.open("rb"), "sha256").hexdigest()
        if digest != expected: sys.exit("JDK checksum mismatch; refusing extraction")
        with tarfile.open(archive) as contents: contents.extractall(toolchains, filter="data")
    jdks = sorted(p for p in toolchains.glob("jdk-21*") if (p / "bin/javac").is_file())
if not jdks: sys.exit("JDK 21 is missing after setup")
version = subprocess.check_output([str(jdks[-1] / "bin/javac"), "-version"], text=True).strip()
if not version.startswith("javac 21"): sys.exit("Unexpected Java compiler version")
print(version, flush=True)
subprocess.run([sys.executable, str(root / "tools/build_mod.py"), "build"], check=True)
subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "bridge/tests", "-v"], cwd=root, check=True)
