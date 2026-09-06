# MUSHclient source export

`mushclient.json` lists every library source, the public header, and the license
that MUSHclient vendors. Update this list when a library file is added or removed.
The inventory check rejects missing files and duplicate destinations.

MUSHclient records an exact commit from this repository. Its import tool copies
Git blob bytes and applies only the ordered patches in its source manifest.
Shared fixes belong here. MUSHclient host behavior belongs in its patch files.

The connection tests accept `--source-dir` so MUSHclient can run the same tests
against its imported, patched source.
