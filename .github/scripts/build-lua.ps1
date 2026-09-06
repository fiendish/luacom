param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Sha256,
    [Parameter(Mandatory = $true)][string]$Destination
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Destination | Out-Null
$Destination = (Resolve-Path $Destination).Path
$archive = Join-Path $Destination 'source.tar.gz'

if ($Version -eq 'LuaJIT') {
    $revision = '24c20c94e7db195b640854619577441f9b4bc6be'
    Invoke-WebRequest "https://github.com/LuaJIT/LuaJIT/archive/$revision.tar.gz" -OutFile $archive
    $source = Join-Path $Destination "LuaJIT-$revision/src"
} else {
    # Release checksums are published at https://www.lua.org/ftp/.
    Invoke-WebRequest "https://www.lua.org/ftp/lua-$Version.tar.gz" -OutFile $archive
    if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $Sha256) {
        throw "SHA256 mismatch for Lua $Version"
    }
    $source = Join-Path $Destination "lua-$Version/src"
}

& tar -xzf $archive -C $Destination
if ($LASTEXITCODE -ne 0) { throw 'Lua source extraction failed' }

Push-Location $source
try {
    if ($Version -eq 'LuaJIT') {
        & cmd /d /c msvcbuild.bat
        if ($LASTEXITCODE -ne 0) { throw 'LuaJIT build failed' }
        Copy-Item luajit.exe (Join-Path $Destination 'lua.exe')
        Copy-Item lua51.dll $Destination
        Copy-Item lua51.lib (Join-Path $Destination 'lua.lib')
    } else {
        $sources = Get-ChildItem -Filter '*.c' |
            Where-Object { $_.Name -notin @('lua.c', 'luac.c', 'print.c') } |
            ForEach-Object { $_.Name }
        & cl /nologo /MD /O2 /DLUA_BUILD_AS_DLL /LD $sources /Fe:lua.dll /link /IMPLIB:lua.lib
        if ($LASTEXITCODE -ne 0) { throw "Lua $Version library build failed" }
        & cl /nologo /MD /O2 /DLUA_BUILD_AS_DLL lua.c lua.lib /Fe:lua.exe
        if ($LASTEXITCODE -ne 0) { throw "Lua $Version interpreter build failed" }
        Copy-Item lua.exe,lua.dll,lua.lib $Destination
    }
} finally {
    Pop-Location
}

"LUA_INCLUDE_DIR=$source" | Out-File $env:GITHUB_ENV -Encoding utf8 -Append
"LUA_LIBRARY=$(Join-Path $Destination 'lua.lib')" | Out-File $env:GITHUB_ENV -Encoding utf8 -Append
"LUA_EXECUTABLE=$(Join-Path $Destination 'lua.exe')" | Out-File $env:GITHUB_ENV -Encoding utf8 -Append
$Destination | Out-File $env:GITHUB_PATH -Encoding utf8 -Append
