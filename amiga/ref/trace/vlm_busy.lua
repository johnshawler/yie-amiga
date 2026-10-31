-- Measures the VLM5030 BSY duration of every speech phrase in MAME, using the game's own start routine ($C89A) driven from a small RAM program.
-- The program does:  LDS #$57F0; ORCC #$50; STA $4000 (flip 0); LDB phrase; JSR $C89A; LDX #0; loop { STA $4F00; LDA #1; ANDA $0000(BSY); BEQ out; LEAX 1,X; BRA loop }; STX $5FF2; flag
-- Output: "phrase iterations t_start t_end" lines (22 cycles per iteration; duration = iterations*22 + 40) to $YT_OUT; t_* are emulated seconds (frame resolution).
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local fo = io.open(os.getenv("YT_OUT") or "vlm_busy.txt", "w")
local prog = {0x10,0xCE,0x57,0xF0, 0x1A,0x50, 0x86,0x00, 0xB7,0x40,0x00, 0xF6,0x5F,0xF1, 0xBD,0xC8,0x9A, 0x8E,0x00,0x00,
              0xB7,0x4F,0x00, 0x86,0x01, 0xB4,0x00,0x00, 0x27,0x04, 0x30,0x01, 0x20,0xF2,
              0xBF,0x5F,0xF2, 0x86,0x01, 0xB7,0x5F,0xF0, 0x20,0xFE}
local f, phrase, state, t_start = 0, tonumber(os.getenv("YT_P0") or "0"), 0, 0
-- optional: log every write to the VLM control port with its exact emulated time (YT_TAP=file)
local tapf = os.getenv("YT_TAP") and io.open(os.getenv("YT_TAP"), "w")
local tap = tapf and mem:install_write_tap(0x4a00, 0x4a00, "vlmtap", function(offset, data, mask)
  tapf:write(string.format("%d %.12f %02x\n", phrase, manager.machine.time:as_double(), data & 0xff)); tapf:flush(); return data
end)
_G.keep_tap = tap   -- the tap is removed when its handle is garbage collected
local maxp = tonumber(os.getenv("YT_MAXP") or "128")
local function start(p)
  for i, b in ipairs(prog) do mem:write_u8(0x5100 + i - 1, b) end
  mem:write_u8(0x5FF0, 0); mem:write_u8(0x5FF1, p)
  cpu.state["PC"].value = 0x5100
  cpu.state["CC"].value = 0x50
  t_start = manager.machine.time:as_double()
end
emu.register_frame_done(function()
  f = f + 1
  if f == 5 then start(phrase); state = 1
  elseif state == 1 and mem:read_u8(0x5FF0) == 1 then
    local x = mem:read_u8(0x5FF2) * 256 + mem:read_u8(0x5FF3)
    fo:write(string.format("%d %d %.9f %.9f\n", phrase, x, t_start, manager.machine.time:as_double())); fo:flush()
    phrase = phrase + 1
    if phrase >= maxp then fo:close(); manager.machine:exit() end
    start(phrase)
  end
end)
_G.keep_tap = tap   -- the tap is removed when its handle is garbage collected
