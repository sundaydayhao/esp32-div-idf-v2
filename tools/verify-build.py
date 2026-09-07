"""Check actual compiler inputs and artifacts, rather than filename declarations."""
import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[1]
build = pathlib.Path(sys.argv[1]).resolve()
board = sys.argv[2].upper()
commands = json.loads((build / "compile_commands.json").read_text())
sources = set()
for item in commands:
    path = pathlib.Path(item["file"]).resolve()
    if any(path.is_relative_to(root / part) for part in ("main", "components")):
        if path.suffix != ".c":
            raise SystemExit(f"Non-C application input: {path}")
        if "-x c++" in item["command"]:
            raise SystemExit(f"C++ compiler override: {path}")
        sources.add(path)
expected = set((root / "components").rglob("*.c")) | {root / "main/app_main.c"}
if expected != sources:
    raise SystemExit(f"Application input mismatch: missing {expected-sources}, extra {sources-expected}")
config = (build / "sdkconfig").read_text()
if f"CONFIG_DIV_BOARD_{board}=y" not in config or 'CONFIG_IDF_TARGET="esp32s3"' not in config:
    raise SystemExit("Incorrect hardware target")
required = ["esp32_div.bin", "esp32_div.elf", "esp32_div.map", "bootloader/bootloader.bin",
            "partition_table/partition-table.bin", "flasher_args.json", "project_description.json"]
for name in required:
    if not (build / name).is_file() or not (build / name).stat().st_size:
        raise SystemExit(f"Missing build output: {name}")
description = json.loads((build / "project_description.json").read_text())
sdk_version = description.get("git_revision", "")
if not re.match(r"^v5\.5(?:\.|$)", sdk_version):
    raise SystemExit(f"Incorrect SDK revision: {sdk_version}; expected ESP-IDF 5.5.x")
size = (build / "esp32_div.bin").stat().st_size
if size > 0x600000:
    raise SystemExit("Application exceeds OTA partition")
print(f"Verified {len(sources)} application C files; {board}; ESP-IDF {sdk_version}; image {size} bytes; OTA capacity 6291456 bytes")
