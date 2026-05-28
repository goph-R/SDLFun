-- App-level entry script for SDLFun.
--
-- Loaded once at boot (main.cpp's scriptRunFile, after assets.lua has
-- registered fonts/textures/etc.). Top-level code runs immediately —
-- we install the scene-stack hook wiring and push the main menu.
--
-- Per-game-session work lives in on_start, which fires from gameInit
-- after the level + entities + physics are up.

local scene = require "engine.scene"
local menu  = require "menu"

-- Wire on_update / on_render / on_keydown / on_mousedown / etc. straight
-- to the scene dispatcher. Anything that needs to happen outside the
-- scene system (FPS counter, global music tick, etc.) can override one
-- of these AFTER install_hooks and call the matching scene.dispatch_*
-- inside it.
scene.install_hooks(_G)

-- Push the main menu. The C side starts in MODE_MENU so this scene
-- begins receiving update/render/input on frame 1.
scene.push(menu.main_menu())

-- Per-game-session hook — fired from gameInit on every New Game. Keep
-- the welcome-message + sound here so the existing demo behavior
-- (intro chirp, ambient track) still fires for each session.
function on_start()
    ui_show_message("Welcome to SDLFun", 3)
    snd_play("jump")
    music_play("ambient")          -- crossfades over the menu's title track
end
