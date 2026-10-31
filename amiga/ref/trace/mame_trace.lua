-- MAME reference trace for Yie Ar Kung-Fu (yiear): per-frame RAM+register dump, cycle-stamped I/O writes, scripted inputs.
-- env: YT_OUT (output prefix), YT_FRAMES (number of frames), YT_INPUTS (input script: lines "frame system p1 p2" hex, active low values, held until next line)
local out = os.getenv("YT_OUT") or "mame_trace"
local nframes = tonumber(os.getenv("YT_FRAMES") or "600")
local inpfile = os.getenv("YT_INPUTS")
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local CPUHZ = 18432000 / 12
local function cyc() return math.floor(manager.machine.time:as_double() * CPUHZ + 0.5) end

local inputs = {}   -- frame -> {sys,p1,p2}
local cur = {0xff, 0xff, 0xff}
if inpfile then
  for l in io.lines(inpfile) do
    local f, s, a, b = l:match("^(%d+)%s+(%x+)%s+(%x+)%s+(%x+)")
    if f then inputs[tonumber(f)] = {tonumber(s, 16), tonumber(a, 16), tonumber(b, 16)} end
  end
end
local fio = io.open(out .. ".io", "w")
local fd = io.open(out .. ".bin", "wb")
local frame = 0
-- writes
tap_w = mem:install_write_tap(0x4000, 0x4fff, "w", function(o, d, m) fio:write(string.format("W %d %04x %02x\n", cyc(), o, d)) end)
-- reads (inputs overridden)
tap_r = mem:install_read_tap(0x4c00, 0x4e03, "r", function(o, d, m)
  local a = o
  local v = d
  if a == 0x4e00 then v = cur[1] elseif a == 0x4e01 then v = cur[2] elseif a == 0x4e02 then v = cur[3] end
  fio:write(string.format("R %d %04x %02x\n", cyc(), a, v))
  return v
end)
tap_r0 = mem:install_read_tap(0x0000, 0x0000, "r", function(o, d, m) fio:write(string.format("R %d 0000 %02x\n", cyc(), d)) return d end)
tap_v = mem:install_read_tap(0xfff8, 0xfffd, "r", function(o, d, m) if o == 0xfff8 or o == 0xfffc then fio:write(string.format("V %d %04x %02x\n", cyc(), o, d)) end return d end)
local function regs()
  local s = cpu.state
  return s["PC"].value, s["X"].value, s["Y"].value, s["U"].value, s["S"].value, s["A"].value, s["B"].value, s["DP"].value, s["CC"].value
end
emu.register_frame_done(function()
  -- frame boundary: dump
  local pc, x, y, u, s, a, b, dp, cc = regs()
  fd:write(string.pack("<I4I8", frame, cyc()))
  fd:write(string.pack("<I2I2I2I2I2BBBB", pc, x, y, u, s, a, b, dp, cc))
  for addr = 0x5000, 0x5fff do fd:write(string.char(mem:read_u8(addr))) end
  frame = frame + 1
  if inputs[frame] then cur = inputs[frame] end
  if frame >= nframes then fio:close(); fd:close(); manager.machine:exit() end
end)
