These tests extract the connection methods from the current LuaCOM source and
run them with substitute COM objects. They check the last-connection contract,
cleanup retries, and callbacks during cleanup. They do not validate Windows COM.

Run with Python 3 and Clang on Linux or macOS:

```sh
python3 src/test/connection_lifetime/run.py
```

The runner enables AddressSanitizer and UndefinedBehaviorSanitizer. It writes
build files to a temporary directory and requires the patched call boundary.
