-- Embed Lua source without tying the header to one Lua bytecode version.
function main(input_path, output_path)
  local input = assert(io.open(input_path, "rb"))
  local source = assert(input:read("*a"))
  assert(input:close())
  source = source:gsub("^#![^\n]*\n", "")

  local bytes = {}
  for index = 1, #source do
    bytes[index] = string.format("0x%02x,%s", source:byte(index),
      index % 16 == 0 and "\n" or "")
  end
  local header = "#pragma once\n\n" ..
    "static const unsigned char luacom5_source_bytes[] = {\n" ..
    table.concat(bytes) .. "\n0x00\n};\n\n" ..
    "static const unsigned int luacom5_source_size = sizeof(luacom5_source_bytes) - 1;\n"

  local previous = io.open(output_path, "rb")
  if previous then
    local content = assert(previous:read("*a"))
    assert(previous:close())
    if content == header then return end
  end
  local output = assert(io.open(output_path, "wb"))
  assert(output:write(header))
  assert(output:close())
end
