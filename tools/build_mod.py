#!/usr/bin/env python3
"""Build with the cloud's supported egress proxy without writing credentials."""
import os
import pathlib
import subprocess
import sys
import urllib.parse

root = pathlib.Path(__file__).resolve().parents[1]
environment = os.environ.copy()
if not environment.get("JAVA_HOME"):
    jdks = sorted(pathlib.Path("/workspace/toolchains").glob("jdk-21*"))
    if jdks:
        environment["JAVA_HOME"] = str(jdks[-1])
options = []
proxy = urllib.parse.urlsplit(environment.get("HTTPS_PROXY", ""))
if proxy.hostname:
    if proxy.username or proxy.password:
        sys.exit("Authenticated proxy needs a supported Java credential binding; do not embed credentials.")
    for scheme in ("https", "http"):
        options += [f"-D{scheme}.proxyHost={proxy.hostname}", f"-D{scheme}.proxyPort={proxy.port or 80}"]
truststore = pathlib.Path("/etc/ssl/certs/java/cacerts")
if proxy.hostname and truststore.exists():
    options += [f"-Djavax.net.ssl.trustStore={truststore}"]
environment["JAVA_TOOL_OPTIONS"] = " ".join(filter(None, [environment.get("JAVA_TOOL_OPTIONS", ""), *options]))
environment.setdefault("GRADLE_USER_HOME", "/workspace/gradle-cache" if pathlib.Path("/workspace").is_dir() else str(pathlib.Path.home() / ".gradle"))
wrapper = "gradlew.bat" if os.name == "nt" else "./gradlew"
sys.exit(subprocess.call([wrapper, *(sys.argv[1:] or ["build"]), "--no-daemon", "--max-workers=2"], cwd=root / "minecraft-mod", env=environment))
