"""Invalidate generated ESP-IDF config when the declared defaults change.

ESP-IDF gives an existing sdkconfig.<env> precedence over sdkconfig.defaults.
pioarduino regenerates defaults, so also invalidate the resolved config on change.
"""
from pathlib import Path
import hashlib

Import("env")
root = Path(env.subst("$PROJECT_DIR"))
name = env.subst("$PIOENV")
settings = "\n".join(str(env.GetProjectOption(key, "")) for key in (
    "platform", "platform_packages", "custom_sdkconfig", "custom_component_remove", "board",
    "board_build.flash_size", "board_build.partitions",
))
script = root / "scripts" / "refresh_sdkconfig.py"
fingerprint = hashlib.sha256(settings.encode() + script.read_bytes()).hexdigest()
stamp = root / ".pio" / "sdkconfig-fingerprints" / name
if not stamp.exists() or stamp.read_text() != fingerprint:
    (root / f"sdkconfig.{name}").unlink(missing_ok=True)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(fingerprint)

# HybridCompile passes project defines to IDF components before applying
# build_unflags. Resolve board overrides once so SDK -Werror sees no duplicates.
# PlatformIO stores build_flags as one string per configuration line.
import re
flags = env.get("BUILD_FLAGS", [])
if isinstance(flags, str):
    flags = flags.splitlines()
seen = set()
resolved = []
for flag in reversed(flags):
    match = re.match(r"^-D\s*([A-Za-z_]\w*)", flag.strip())
    if match:
        if match.group(1) in seen:
            continue
        seen.add(match.group(1))
    resolved.append(flag)
env.Replace(BUILD_FLAGS=list(reversed(resolved)))

# HybridCompile copies the rebuilt libraries and memory.ld, but leaves the
# precompiled sections.ld behind. Sleep/FreeRTOS IRAM mappings depend on the
# resolved sdkconfig, so preserve the matching generated linker script before
# HybridCompile deletes its temporary IDF build directory.
def copy_linker_sections(source, target, env):
    import shutil
    sections = Path(env.subst("$BUILD_DIR")) / "sections.ld"
    if sections.exists():
        libraries = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"))
        destination = libraries / env.BoardConfig().get("build.mcu") / "ld" / "sections.ld"
        shutil.copy2(sections, destination)

env.AddPostAction("checkprogsize", copy_linker_sections)
