-- hypr-xcsd: X11 apps that draw their own title bar (games, launchers, ...) can be moved
-- and resized by it, and lose Omarchy's extra title bar.
--
-- Source, build and design notes: ~/Projects/hypr-xcsd. The plugin tags such
-- windows "x11-csd"; the rule below hides Omarchy's bar for that tag.
--
-- Runs on every config load: Hyprland unloads a plugin the config stops
-- asking for. With no build for the running Hyprland (first run, or right
-- after a Hyprland upgrade) it starts a background build, which reloads the
-- config when done. Until then, or if the build fails, nothing changes:
-- Omarchy's bars stay.

local dir = os.getenv("HOME") .. "/.local/lib/hypr-xcsd/"
-- The instance signature starts with the running Hyprland's commit; a build is
-- only ever loaded into the exact Hyprland it was compiled against.
local running = (os.getenv("HYPRLAND_INSTANCE_SIGNATURE") or ""):match("^(%x+)_")

local function read(path)
  local f = io.open(path, "r")
  if not f then return nil end
  local s = f:read("l")
  f:close()
  return s
end

local function exists(path)
  local f = io.open(path, "rb")
  if f then f:close() end
  return f ~= nil
end

if running then
  local so = read(dir .. running .. ".current")
  if so and exists(dir .. so) then
    local ok, err = pcall(hl.plugin.load, dir .. so)
    if not ok then print("hypr-xcsd: " .. tostring(err)) end
  else
    hl.exec_cmd(os.getenv("HOME") .. "/Projects/hypr-xcsd/build.sh " .. running)
  end
end

hl.window_rule({ name = "hypr-xcsd-no-titlebar", match = { tag = "x11-csd" }, ["hyprbars:no_bar"] = true })
