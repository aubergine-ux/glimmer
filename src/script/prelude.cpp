#include "script/vm.hpp"

namespace glim::script {

// Helpers written in Lua itself. They run inside the cart's budget like any
// other Lua code, which keeps their cost honest.
const char* const kPrelude = R"lua(
local floor, ceil, mmin, mmax = math.floor, math.ceil, math.min, math.max
local insert, remove = table.insert, table.remove

abs = math.abs
sqrt = math.sqrt
min = mmin
max = mmax
sub = string.sub
chr = string.char

function lerp(a, b, t) return a + (b - a) * t end
function clamp(v, lo, hi) return mmax(lo, mmin(hi, v)) end

function ord(s, i, n)
  i = i or 1
  return string.byte(s, i, i + (n or 1) - 1)
end

function tonum(v)
  if type(v) == "boolean" then return v and 1 or 0 end
  return tonumber(v)
end

function add(t, v, i)
  if i then insert(t, i, v) else t[#t + 1] = v end
  return v
end

function del(t, v)
  for i = 1, #t do
    if t[i] == v then return remove(t, i) end
  end
end

function deli(t, i) return remove(t, i or #t) end

function count(t, v)
  if v == nil then return #t end
  local n = 0
  for i = 1, #t do
    if t[i] == v then n = n + 1 end
  end
  return n
end

-- Safe to del() the current item while iterating.
function all(t)
  local i, cur = 0, nil
  return function()
    if i > 0 and t[i] ~= cur then i = i - 1 end
    i = i + 1
    cur = t[i]
    return cur
  end
end

function foreach(t, f)
  for v in all(t) do f(v) end
end

function split(s, sep, convert)
  sep = sep or ","
  local out = {}
  if sep == "" then
    for i = 1, #s do out[i] = s:sub(i, i) end
  else
    local start = 1
    while true do
      local a, b = string.find(s, sep, start, true)
      if not a then
        out[#out + 1] = s:sub(start)
        break
      end
      out[#out + 1] = s:sub(start, a - 1)
      start = b + 1
    end
  end
  if convert ~= false then
    for i = 1, #out do out[i] = tonumber(out[i]) or out[i] end
  end
  return out
end
)lua";

}  // namespace glim::script
