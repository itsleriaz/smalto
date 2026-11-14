-- Image files other than .aseprite / PNG for the import tests, written by
-- Aseprite. Run by generate.lua; can also run alone the same way:
--
--   aseprite -b --script-param out=tests/corpus --script tests/corpus/images.lua
--
-- anim.gif: 3 frames (100 / 250 / 40 ms) with transparency; anim.rgba holds
-- Aseprite's render of each frame. still.bmp / .tga / .jpg are one opaque
-- frame; still.rgba is its render.

local OUT = OUT_DIR or app.params["out"] or "."
local pc = app.pixelColor

local function fill(img, x, y, w, h, v)
  for yy = y, y + h - 1 do
    for xx = x, x + w - 1 do
      img:drawPixel(xx, yy, v)
    end
  end
end

local function writeRgba(spr, name)
  local f = assert(io.open(OUT .. "/" .. name .. ".rgba", "wb"))
  for i = 1, #spr.frames do
    local img = Image(spr.width, spr.height, ColorMode.RGB)
    img:drawSprite(spr, i)
    for y = 0, img.height - 1 do
      for x = 0, img.width - 1 do
        local c = img:getPixel(x, y)
        f:write(string.char(pc.rgbaR(c), pc.rgbaG(c), pc.rgbaB(c), pc.rgbaA(c)))
      end
    end
  end
  f:close()
end

-- Animated GIF: a square moving over a transparent background, a fixed bar.
do
  local spr = Sprite(16, 12, ColorMode.RGB)
  app.activeSprite = spr
  for _ = 2, 3 do spr:newEmptyFrame() end
  local durations = {0.1, 0.25, 0.04}
  local l = spr.layers[1]
  for f = 1, 3 do
    spr.frames[f].duration = durations[f]
    local img = Image(16, 12, ColorMode.RGB)
    fill(img, 0, 10, 16, 2, pc.rgba(40, 40, 60))
    fill(img, 2 + f * 3, 2, 5, 5, pc.rgba(230, 80, 40))
    fill(img, 3 + f * 3, 3, 2, 2, pc.rgba(255, 220, 90))
    spr:newCel(l, f, img, Point(0, 0))
  end
  spr:saveCopyAs(OUT .. "/anim.gif")
  writeRgba(spr, "anim")
  spr:close()
  print("wrote anim.gif")
end

-- One opaque frame in flat blocks (JPEG compresses them with little loss).
do
  local spr = Sprite(16, 16, ColorMode.RGB)
  app.activeSprite = spr
  local img = Image(16, 16, ColorMode.RGB)
  fill(img, 0, 0, 8, 8, pc.rgba(200, 40, 40))
  fill(img, 8, 0, 8, 8, pc.rgba(40, 160, 60))
  fill(img, 0, 8, 8, 8, pc.rgba(40, 70, 200))
  fill(img, 8, 8, 8, 8, pc.rgba(240, 240, 230))
  spr:newCel(spr.layers[1], 1, img, Point(0, 0))
  for _, ext in ipairs({"bmp", "tga", "jpg"}) do
    spr:saveCopyAs(OUT .. "/still." .. ext)
  end
  writeRgba(spr, "still")
  spr:close()
  print("wrote still.bmp / .tga / .jpg")
end
