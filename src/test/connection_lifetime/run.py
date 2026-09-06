"""Check connection ownership with the patched source and substitute COM objects."""
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1] / 'library'

def extract(text, signature):
    start = text.index(signature)
    pos = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start:pos]

source = (ROOT / 'tLuaCOM.cpp').read_text()
header = (ROOT / 'tLuaCOM.h').read_text()
legacy = extract(source, 'void tLuaCOM::releaseConnection()')
assert 'last_connection_interface' in legacy and 'pending.splice' in legacy
assert 'last_connection_cookie  = 0;' in source
assert 'IID last_connection_interface;' in header
assert 'DWORD last_connection_cookie;' in header
assert 'lua_pcall' in extract(source, 'int tLuaCOM::call(')

shim = (HERE / 'shim.cpp').read_text()
shim = shim.replace('/* POINTER */', extract((ROOT / 'tCOMUtil.h').read_text(), 'template <class T>') + ';')
functions = '\n'.join(extract(source, name) for name in [
    'DWORD tLuaCOM::addConnection(',
    'void tLuaCOM::releaseConnection()',
    'void tLuaCOM::releaseConnection(tLuaCOM* server, DWORD cookie)',
    'HRESULT tLuaCOM::releaseConnections()',
    'void tLuaCOM::checkComObject() const',
    'void tLuaCOM::releaseComObject()',
])
shim = shim.replace('/* FUNCTIONS */', functions)
with tempfile.TemporaryDirectory(prefix='luacom-connection-tests-') as directory:
    output = Path(directory)
    cpp = output / 'test.cpp'
    cpp.write_text(shim + (HERE / 'tests.cpp').read_text())
    exe = output / 'test'
    subprocess.run(['clang++', '-std=c++14', '-g', '-O1', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
