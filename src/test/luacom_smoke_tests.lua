local luacom = require("luacom")

-- These helpers must load from the source embedded in the DLL.
assert(type(luacom.CreateInprocObject) == "function")
assert(type(luacom.GetType) == "function")
assert(type(luacom.pairs) == "function")

local original_abort = luacom.config.abort_on_API_error
local original_method_abort = luacom.config.abort_on_error
luacom.config.abort_on_API_error = true
luacom.config.abort_on_error = true

-- Scripting.Dictionary is part of Windows and needs no external application.
local dictionary = assert(luacom.CreateInprocObject("Scripting.Dictionary"))
assert(luacom.GetType(dictionary) == "LuaCOM")
dictionary:Add("first", "value")
dictionary:Add("second", 42)
assert(dictionary.Count == 2)
assert(dictionary:Exists("first"))
assert(dictionary:Item("first") == "value")
assert(dictionary:Item("second") == 42)

local keys = {}
for _, key in luacom.pairs(dictionary) do
  keys[key] = true
end
assert(keys.first and keys.second)

-- File collections also expose enumeration through a property.
local filesystem = assert(luacom.CreateInprocObject("Scripting.FileSystemObject"))
local folder = filesystem:GetFolder(".")
local files = folder.Files
local file_count = 0
for _, file in luacom.pairs(files) do
  assert(type(file.Name) == "string" and #file.Name > 0)
  file_count = file_count + 1
end
assert(file_count > 0 and file_count == files.Count)
luacom.ReleaseComObject(files)
luacom.ReleaseComObject(folder)
luacom.ReleaseComObject(filesystem)

local typeinfo = assert(luacom.GetTypeInfo(dictionary))
assert(luacom.GetType(typeinfo) == "ITypeInfo")
assert(type(typeinfo:GetDocumentation()) == "table")

dictionary:Remove("first")
assert(dictionary.Count == 1)
dictionary:RemoveAll()
assert(dictionary.Count == 0)

luacom.ReleaseComObject(dictionary)
assert(not pcall(function() return dictionary.Count end))
luacom.config.abort_on_API_error = original_abort
luacom.config.abort_on_error = original_method_abort

print("LuaCOM smoke tests passed")
