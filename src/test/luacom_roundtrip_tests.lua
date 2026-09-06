local luacom = require("luacom")
local config = luacom.config
assert(type(config) == "table")

local original_table_variants = luacom.TableVariants
local original_date_format = luacom.DateFormat
local original_code_page = luacom.GetCodepage()
local original_abort_on_api_error = config.abort_on_API_error
local original_last_error = config.last_error

local function roundtrip_variant(variant_type, value)
  luacom.TableVariants = true
  local result = luacom.RoundTrip({Type = variant_type, Value = value})
  -- RoundTrip returns the scalar value, even when TableVariants is enabled.
  assert(type(result) ~= "table")
  return result
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

for _, value in ipairs({
  {"int1", -128, 127},
  {"uint1", 0, 255},
  {"int2", -32768, 32767},
  {"uint2", 0, 65535},
  {"int4", -2147483648, 2147483647},
  {"uint4", 0, 4294967295},
  {"int", -2147483648, 2147483647},
  {"uint", 0, 4294967295},
}) do
  assert(roundtrip_variant(value[1], value[2]) == value[2])
  assert(roundtrip_variant(value[1], value[3]) == value[3])
  assert(roundtrip_variant(value[1], tostring(value[3])) == value[3])
end

for _, value in ipairs({
  {"int8", "-9223372036854775808"},
  {"int8", "9223372036854775807"},
  {"uint8", "18446744073709551615"},
}) do
  assert(tostring(roundtrip_variant(value[1], value[2])) == value[2])
end

local exact_string_array = luacom.RoundTrip({
  Type = "array of int8",
  Value = {"-9223372036854775808", "9223372036854775807"},
})
assert(tostring(exact_string_array[1]) == "-9223372036854775808")
assert(tostring(exact_string_array[2]) == "9223372036854775807")

local unsigned_string_array = luacom.RoundTrip({
  Type = "array of uint8",
  Value = {"18446744073709551615"},
})
assert(unsigned_string_array[1] == "18446744073709551615")

for _, value in ipairs({
  {"array of int1", {-128, 127}},
  {"array of uint2", {0, 65535}},
  {"array of uint4", {0, 4294967295}},
  {"array of uint", {0, 4294967295}},
  {"array of string", {"first", "second"}},
}) do
  local result = luacom.RoundTrip({Type = value[1], Value = value[2]})
  assert(#result == #value[2])
  for index, expected in ipairs(value[2]) do
    assert(result[index] == expected)
  end
end

local empty_array = luacom.RoundTrip({Type = "array", Value = {}})
assert(type(empty_array) == "table" and next(empty_array) == nil)

luacom.SetCodepage(65001)
for _, value in ipairs({"", "plain text", "A\0B", "caf\195\169"}) do
  assert(luacom.RoundTrip(value) == value)
end
assert(luacom.RoundTrip({Type = "string", Value = "typed text"}) == "typed text")
luacom.SetCodepage(original_code_page)

assert(luacom.RoundTrip(true) == true)
assert(luacom.RoundTrip(false) == false)
assert(select("#", luacom.RoundTrip(nil)) == 1)
assert(luacom.RoundTrip(nil) == nil)

local success_error_sentinel = "round-trip success sentinel"
config.last_error = success_error_sentinel
assert(luacom.RoundTrip("valid with saved error") == "valid with saved error")
assert(luacom.config == config)
assert(config.last_error == success_error_sentinel)

config.abort_on_API_error = true
for _, value in ipairs({
  {"int1", 128},
  {"uint1", -1},
  {"int2", 32768},
  {"uint2", 65536},
  {"int4", 2147483648},
  {"uint4", 4294967296},
  {"int", 0.5},
  {"uint", -1},
  {"int8", "9223372036854775808"},
  {"int8", "-9223372036854775809"},
  {"uint8", "18446744073709551616"},
  {"uint8", -1},
  {"int8", "not an integer"},
  {"int8", 0.5},
}) do
  local ok, message = pcall(luacom.RoundTrip, {Type = value[1], Value = value[2]})
  assert(not ok)
  assert(type(message) == "string" and #message > 0)
  assert(luacom.config == config)
  local reported_error = config.last_error
  assert(type(reported_error) == "string" and #reported_error > 0)
  assert(luacom.RoundTrip("valid after error") == "valid after error")
  assert(config.last_error == reported_error)
end

config.abort_on_API_error = false
assert(select("#", luacom.RoundTrip({Type = "int4", Value = 0.5})) == 0)
assert(luacom.config == config)
local reported_error = config.last_error
assert(type(reported_error) == "string" and #reported_error > 0)
assert(luacom.RoundTrip("valid after reported error") == "valid after reported error")
assert(config.last_error == reported_error)
config.abort_on_API_error = original_abort_on_api_error

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

local function assert_historical_milliseconds(year, milliseconds)
  local value = {
    Year = year,
    Month = 1,
    Day = 2,
    Hour = 0,
    Minute = 0,
    Second = 0,
    Milliseconds = milliseconds,
  }
  local result = luacom.RoundTrip(value)
  assert(result.Year == value.Year)
  assert(result.Month == value.Month)
  assert(result.Day == value.Day)
  assert(result.Hour == value.Hour)
  assert(result.Minute == value.Minute)
  assert(result.Second == value.Second)
  assert(result.Milliseconds == value.Milliseconds)
end

for _, year in ipairs({100, 1600}) do
  for _, milliseconds in ipairs({499, 500, 999}) do
    assert_historical_milliseconds(year, milliseconds)
  end
end

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

for _, year in ipairs({100, 1600}) do
  date.Year = year
  date.Month = 1
  date.Day = 2
  date.Hour = 0
  date.Minute = 0
  date.Second = 0
  date.Milliseconds = 500
  assert(#luacom.RoundTrip(date) > 0)
  date.Milliseconds = 999
  assert(#luacom.RoundTrip(date) > 0)
end

luacom.SetCodepage(original_code_page)
assert(luacom.GetCodepage() == original_code_page)

luacom.TableVariants = original_table_variants
luacom.DateFormat = original_date_format
assert(luacom.config == config)
config.abort_on_API_error = original_abort_on_api_error
config.last_error = original_last_error

print("LuaCOM round-trip tests passed")
