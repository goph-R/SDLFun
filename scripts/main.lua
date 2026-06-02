-- App-level entry script for SDLFun.
--
-- Loaded once at boot (main.cpp's scriptRunFile, after assets.lua has
-- registered fonts/textures/etc.). Top-level code runs immediately —
-- we install the scene-stack hook wiring and push the main menu.
--
-- Per-game-session work lives in onStart, which fires from gameInit
-- after the level + entities + physics are up.

local scene = require "engine.scene"
local menu  = require "menu"

-- Wire onUpdate / onRender / onKeyDown / onMouseDown / etc. straight
-- to the scene dispatcher. Anything that needs to happen outside the
-- scene system (FPS counter, global music tick, etc.) can override one
-- of these AFTER installHooks and call the matching scene.dispatch_*
-- inside it.
scene.installHooks(_G)

-- Push the main menu. The C side starts in MODE_MENU so this scene
-- begins receiving update/render/input on frame 1.
scene.push(menu.mainMenu())

-- Per-game-session hook — fired from gameInit on every New Game. Keep
-- the welcome-message + sound here so the existing demo behavior
-- (intro chirp, ambient track) still fires for each session.
function onStart()
    uiShowMessage("Welcome to SDLFun", 3)
    soundPlay("jump")
    musicPlay("ambient")          -- crossfades over the menu's title track
end
