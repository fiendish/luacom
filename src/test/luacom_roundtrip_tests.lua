local luacom = require("luacom")

local original_table_variants = luacom.TableVariants
local original_date_format = luacom.DateFormat
local original_code_page = luacom.GetCodepage()

local function roundtrip_variant(variant_type, value)
  luacom.TableVariants = true
  local result = luacom.RoundTrip({Type = variant_type, Value = value})
  assert(type(result) == "table")
  assert(result.Type == variant_type)
  return result.Value
end

assert(roundtrip_variant("int8", -42) == -42)
assert(roundtrip_variant("uint8", 42) == 42)

if math.type and math.maxinteger and math.maxinteger > 9007199254740992 then
  local exact_integer = 9007199254740993
  local signed_result = roundtrip_variant("int8", -exact_integer)
  local unsigned_result = roundtrip_variant("uint8", exact_integer)

  assert(signed_result == -exact_integer)
  assert(unsigned_result == exact_integer)
  assert(math.type(signed_result) == "integer")
  assert(math.type(unsigned_result) == "integer")

  local signed_array = luacom.RoundTrip({
    Type = "array of int8",
    Value = {-exact_integer, exact_integer},
  })
  local unsigned_array = luacom.RoundTrip({
    Type = "array of uint8",
    Value = {exact_integer},
  })

  assert(signed_array[1] == -exact_integer)
  assert(signed_array[2] == exact_integer)
  assert(unsigned_array[1] == exact_integer)
  assert(math.type(signed_array[1]) == "integer")
  assert(math.type(signed_array[2]) == "integer")
  assert(math.type(unsigned_array[1]) == "integer")
end

local date = {
  Year = 2024,
  Month = 6,
  Day = 20,
  Hour = 12,
  Minute = 1,
  Second = 1,
  Milliseconds = 499,
}

luacom.TableVariants = false
luacom.DateFormat = "table"
local date_result = luacom.RoundTrip(date)
assert(date_result.Year == date.Year)
assert(date_result.Month == date.Month)
assert(date_result.Day == date.Day)
assert(date_result.Hour == date.Hour)
assert(date_result.Minute == date.Minute)
assert(date_result.Second == date.Second)
assert(date_result.Milliseconds == date.Milliseconds)

local historical_date = {
  Year = 1899,
  Month = 12,
  Day = 29,
  Hour = 12,
  Minute = 34,
  Second = 56,
  Milliseconds = 499,
}
local historical_result = luacom.RoundTrip(historical_date)
assert(historical_result.Year == historical_date.Year)
assert(historical_result.Month == historical_date.Month)
assert(historical_result.Day == historical_date.Day)
assert(historical_result.Hour == historical_date.Hour)
assert(historical_result.Minute == historical_date.Minute)
assert(historical_result.Second == historical_date.Second)
assert(historical_result.Milliseconds == historical_date.Milliseconds)

local automation_epoch = {
  Year = 1899,
  Month = 12,
  Day = 30,
  Hour = 0,
  Minute = 0,
  Second = 0,
  Milliseconds = 0,
}
local epoch_result = luacom.RoundTrip(automation_epoch)
assert(epoch_result.Year == automation_epoch.Year)
assert(epoch_result.Month == automation_epoch.Month)
assert(epoch_result.Day == automation_epoch.Day)
assert(epoch_result.Hour == automation_epoch.Hour)
assert(epoch_result.Minute == automation_epoch.Minute)
assert(epoch_result.Second == automation_epoch.Second)
assert(epoch_result.Milliseconds == automation_epoch.Milliseconds)

luacom.DateFormat = "string_ms_accurate"
date.Second = 59
date.Milliseconds = 499
local formatted_before_boundary = luacom.RoundTrip(date)
date.Milliseconds = 500
local formatted_after_boundary = luacom.RoundTrip(date)
assert(type(formatted_before_boundary) == "string")
assert(type(formatted_after_boundary) == "string")
assert(#formatted_before_boundary > 0)
assert(#formatted_after_boundary > 0)
assert(formatted_before_boundary ~= formatted_after_boundary)

luacom.SetCodepage(original_code_page)
assert(luacom.GetCodepage() == original_code_page)

luacom.TableVariants = original_table_variants
luacom.DateFormat = original_date_format

print("LuaCOM round-trip tests passed")
