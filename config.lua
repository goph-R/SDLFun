-- config.lua — read once at startup, before SDL / OpenGL init.
--
-- Precedence: built-in defaults < config.lua < command-line args.
-- Command-line flags that override these:
--   -w N         set width
--   -h N         set height
--   -fullscreen  force fullscreen on
--   -windowed    force fullscreen off
--
-- Set width or height to 0 to use the current desktop resolution.
-- That sentinel is only honored when fullscreen = true; in windowed
-- mode the engine falls back to the minimum (320×240).

return {
    display = {
        width      = 640,
        height     = 480,
        fullscreen = false,
    },
}
