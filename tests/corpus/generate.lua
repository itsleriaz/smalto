-- Generates the .aseprite round-trip corpus with Aseprite itself.
--
--   aseprite -b --script-param out=tests/corpus --script tests/corpus/generate.lua
--
-- For every NAME.aseprite it also writes NAME.rgba: Aseprite's own render of
-- each frame (RGBA8, frames concatenated), which the tests compare against.

local OUT = OUT_DIR or app.params["out"] or "."
local pc = app.pixelColor

local function fill(img, x, y, w, h, v)
  for yy = y, y + h - 1 do
    for xx = x, x + w - 1 do
      img:drawPixel(xx, yy, v)
    end
  end
end

local function cel(spr, layer, frame, w, h, pos, paint)
  local img = Image(w, h, spr.colorMode)
  paint(img)
  return spr:newCel(layer, frame, img, pos)
end

local function save(spr, name, png)
  spr:saveAs(OUT .. "/" .. name .. ".aseprite")
  -- PNG copies for import tests; several frames give NAME1.png, NAME2.png, ...
  if png then spr:saveCopyAs(OUT .. "/" .. png .. ".png") end
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
  print("wrote " .. name, #spr.layers .. " top layers", #spr.frames .. " frames")
  spr:close()
end

-- RGBA: groups, opacity, blend modes, hidden/locked layers, linked cels,
-- z-index, tags, slices, user data with properties, grid and pixel ratio.
do
  local spr = Sprite(24, 16, ColorMode.RGB)
  app.activeSprite = spr
  for _ = 2, 3 do spr:newEmptyFrame() end

  local bg = spr.layers[1]
  bg.name = "bg"
  local colors = { pc.rgba(40, 60, 200), pc.rgba(200, 60, 40), pc.rgba(30, 160, 90) }
  for f = 1, 3 do
    cel(spr, bg, f, 20, 12, Point(2, 2), function(img)
      fill(img, 0, 0, 20, 12, colors[f])
      fill(img, f, f, 4, 4, pc.rgba(255, 255, 255, 128))
    end)
  end

  local mult = spr:newLayer()
  mult.name = "multiply"
  mult.blendMode = BlendMode.MULTIPLY
  mult.opacity = 200
  for f = 1, 3 do
    local c = cel(spr, mult, f, 10, 8, Point(f * 3, 4), function(img)
      fill(img, 0, 0, 10, 8, pc.rgba(250, 200, 90, 220))
    end)
    if f == 2 then c.opacity = 128 end
    if f == 3 then c.zIndex = -1 end
  end

  local grp = spr:newGroup()
  grp.name = "group"
  grp.opacity = 180
  grp.blendMode = BlendMode.SCREEN
  local a = spr:newLayer()
  a.name = "overlay child"
  a.blendMode = BlendMode.OVERLAY
  a.parent = grp
  local b = spr:newLayer()
  b.name = "hidden child"
  b.isVisible = false
  b.parent = grp
  for f = 1, 3 do
    cel(spr, a, f, 6, 6, Point(14, 1 + f * 2), function(img) fill(img, 0, 0, 6, 6, pc.rgba(90, 30, 160, 200)) end)
    cel(spr, b, f, 24, 16, Point(0, 0), function(img) fill(img, 0, 0, 24, 16, pc.rgba(255, 0, 0)) end)
  end

  local locked = spr:newLayer()
  locked.name = "locked ünïcode"
  locked.isEditable = false
  locked.data = "layer data"
  locked.color = Color(10, 20, 30, 255)
  locked.properties.number = 42
  locked.properties.nested = { flag = true, text = "hi", list = { 1, 2, 3 } }
  local lc = cel(spr, locked, 1, 3, 3, Point(-1, -1), function(img) fill(img, 0, 0, 3, 3, pc.rgba(0, 0, 0)) end)
  lc.data = "cel data"
  lc.color = Color(1, 2, 3, 4)

  -- Frame 4 links the cels of frame 3.
  spr:newEmptyFrame()
  app.range:clear()
  app.range.layers = { bg, mult, a, b }
  app.range.frames = { spr.frames[3], spr.frames[4] }
  app.command.LinkCels()

  local durations = { 0.1, 0.15, 0.08, 0.2 }
  for f = 1, 4 do spr.frames[f].duration = durations[f] end

  local walk = spr:newTag(1, 2)
  walk.name = "walk"
  walk.aniDir = AniDir.PING_PONG
  walk.repeats = 2
  walk.color = Color(255, 0, 0)
  walk.data = "tag data"
  local idle = spr:newTag(3, 4)
  idle.name = "idle"
  idle.aniDir = AniDir.REVERSE

  local slice = spr:newSlice(Rectangle(2, 2, 10, 10))
  slice.name = "hitbox"
  slice.center = Rectangle(1, 1, 8, 8)
  slice.pivot = Point(5, 5)
  slice.data = "slice data"

  spr.data = "sprite data"
  spr.gridBounds = Rectangle(2, 3, 8, 8)
  spr.pixelRatio = Size(2, 1)
  save(spr, "rgba_features", "rgba_seq")
end

-- Every blend mode: frame i shows the base plus layer i only.
do
  local modes = {
    BlendMode.NORMAL, BlendMode.MULTIPLY, BlendMode.SCREEN, BlendMode.OVERLAY,
    BlendMode.DARKEN, BlendMode.LIGHTEN, BlendMode.COLOR_DODGE, BlendMode.COLOR_BURN,
    BlendMode.HARD_LIGHT, BlendMode.SOFT_LIGHT, BlendMode.DIFFERENCE, BlendMode.EXCLUSION,
    BlendMode.HSL_HUE, BlendMode.HSL_SATURATION, BlendMode.HSL_COLOR, BlendMode.HSL_LUMINOSITY,
    BlendMode.ADDITION, BlendMode.SUBTRACT, BlendMode.DIVIDE,
  }
  local spr = Sprite(16, 16, ColorMode.RGB)
  app.activeSprite = spr
  for _ = 2, #modes do spr:newEmptyFrame() end
  local base = spr.layers[1]
  base.name = "base"
  for f = 1, #modes do
    cel(spr, base, f, 16, 16, Point(0, 0), function(img)
      for y = 0, 15 do
        for x = 0, 15 do
          img:drawPixel(x, y, pc.rgba(x * 16, y * 16, 255 - x * 8, x < 2 and 0 or 255))
        end
      end
    end)
  end
  for i, mode in ipairs(modes) do
    local l = spr:newLayer()
    l.name = "mode " .. i
    l.blendMode = mode
    l.opacity = 230
    cel(spr, l, i, 16, 16, Point(0, 0), function(img)
      for y = 0, 15 do
        for x = 0, 15 do
          img:drawPixel(x, y, pc.rgba(255 - y * 16, 128, y * 16, 64 + x * 12))
        end
      end
    end)
  end
  save(spr, "blend_modes")
end

-- Indexed: non-zero transparent index, named palette, opaque background.
do
  local spr = Sprite(16, 16, ColorMode.INDEXED)
  app.activeSprite = spr
  local pal = Palette(8)
  for i = 0, 7 do pal:setColor(i, Color(i * 32, 255 - i * 32, 128, 255)) end
  spr:setPalette(pal)
  spr.transparentColor = 3

  local bg = spr.layers[1]
  bg.name = "background"
  cel(spr, bg, 1, 16, 16, Point(0, 0), function(img)
    for y = 0, 15 do
      for x = 0, 15 do img:drawPixel(x, y, (x + y) % 8) end
    end
  end)
  app.activeLayer = bg
  app.command.BackgroundFromLayer()
  -- The conversion fills transparent pixels with the background color;
  -- repaint so the background also uses the transparent index, opaquely.
  local bgImage = bg:cel(1).image
  for y = 0, 15 do
    for x = 0, 15 do bgImage:drawPixel(x, y, (x + y) % 8) end
  end

  local top = spr:newLayer()
  top.name = "top"
  cel(spr, top, 1, 16, 16, Point(0, 0), function(img)
    for y = 0, 15 do
      for x = 0, 15 do img:drawPixel(x, y, (x < 8) and 3 or 5) end
    end
  end)
  save(spr, "indexed", "indexed")
end

-- Grayscale with alpha.
do
  local spr = Sprite(12, 12, ColorMode.GRAY)
  app.activeSprite = spr
  spr:newEmptyFrame()
  local l = spr.layers[1]
  for f = 1, 2 do
    cel(spr, l, f, 10, 10, Point(1, 1), function(img)
      for y = 0, 9 do
        for x = 0, 9 do img:drawPixel(x, y, pc.graya(x * 25, 255 - y * 20 * f // 2)) end
      end
    end)
  end
  local l2 = spr:newLayer()
  l2.opacity = 128
  cel(spr, l2, 2, 4, 4, Point(4, 4), function(img) fill(img, 0, 0, 4, 4, pc.graya(255, 255)) end)
  save(spr, "grayscale")
end

-- Tilemap layer with a tileset (rendering not supported by Smalto yet).
do
  local spr = Sprite(16, 16, ColorMode.RGB)
  app.activeSprite = spr
  app.command.NewLayer { tilemap = true, gridBounds = Rectangle(0, 0, 4, 4) }
  local tl = app.activeLayer
  tl.name = "tiles"
  local ts = tl.tileset
  ts.name = "set"
  for i = 1, 3 do
    local t = spr:newTile(ts)
    fill(t.image, 0, 0, 4, 4, pc.rgba(i * 80, 40, 255 - i * 80))
    t.data = "tile " .. i
  end
  local map = Image(4, 4, ColorMode.TILEMAP)
  for y = 0, 3 do
    for x = 0, 3 do map:drawPixel(x, y, pc.tile((x + y) % 4, 0)) end
  end
  spr:newCel(tl, 1, map, Point(0, 0))
  save(spr, "tilemap")
end

-- GIF / BMP / TGA / JPEG fixtures, next to this script.
dofile((debug.getinfo(1, "S").source:match("^@?(.*[/\\])") or "") .. "images.lua")
