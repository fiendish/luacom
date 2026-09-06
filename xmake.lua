
add_rules("mode.debug", "mode.release")
add_requires("lua 5.4", {configs = {shared = true}})

target 'luacom'
    set_kind("shared")
    set_languages("cxx11")
    on_load(function (target)
        target:add("includedirs", path.join(target:autogendir(), "include"))
    end)
    before_build(function (target)
        local generated_dir = path.join(target:autogendir(), "include")
        os.mkdir(generated_dir)
        import("mak.embed", {rootdir = os.projectdir()}).main(
            path.join(os.projectdir(), "src/library/luacom5.lua"),
            path.join(generated_dir, "luacom.loh"))
    end)
    add_packages('lua')

    -- add files
    add_files("src/library/*.cpp")
    add_files("src/dll/*.cpp")
    add_files("src/dll/luacom_dll.def")

    add_defines('LUA_COMPAT_5_3', 'LUACOM_DLL="luacom.dll"')

    add_includedirs 'include'
    add_includedirs 'src/library'
    add_links('advapi32', 'ole32', 'user32', 'shell32', 'gdi32', 'shlwapi', 'uuid', 'winspool', 'oleaut32', 'htmlhelp')
