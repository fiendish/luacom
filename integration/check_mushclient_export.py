"""Check the complete source inventory exported to MUSHclient."""
import json
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
manifest = json.loads((ROOT / "integration/mushclient.json").read_text())
if manifest.keys() != {"version", "files"} or manifest["version"] != 1:
    raise ValueError("Unsupported MUSHclient export manifest")
files = manifest["files"]
expected = {p.relative_to(ROOT).as_posix() for p in (ROOT / "src/library").rglob("*") if p.is_file()}
expected.update({"include/luacom.h", "COPYRIGHT"})
if set(files) != expected:
    raise ValueError("MUSHclient export must include every library file, public header, and license")
if len(set(files.values())) != len(files):
    raise ValueError("Duplicate MUSHclient export destination")
for source, destination in files.items():
    if PurePosixPath(destination).parts != (destination,) or destination in {".", ".."}:
        raise ValueError("Export destinations must be plain file names")
    if (ROOT / source).is_symlink():
        raise ValueError("Export files must not be symbolic links")
print(f"MUSHclient export inventory passed: {len(files)} files")
