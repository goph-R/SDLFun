-- Music wrapper that honors the persisted music_on / music_vol settings
-- so callsites can request a track without consulting optGet themselves.
--
-- Usage:
--   local audio = require "audio"
--   audio.applyPersisted()             -- once at boot, before any play
--   audio.play("title", 1.0, true)     -- play if music_on, else remember
--   audio.setEnabled(on)               -- from the Music checkbox
--   audio.setVolume(v)                 -- from the volume slider

local M = {}

-- Last requested track. Stored even when music_on is false so toggling
-- the checkbox back on can resume whatever the game last asked for
-- (title in the menu, ambient in-game).
local current = nil

local function savedVolume()
    return optGet("music_vol", 0.7)
end

function M.play(name, fade, loop)
    current = { name = name, fade = fade, loop = loop }
    if optGet("music_on", true) then
        musicPlay(name, fade, loop)
        musicVolume(savedVolume())
    end
end

function M.setVolume(v)
    optSet("music_vol", v)
    if optGet("music_on", true) then
        musicVolume(v)
    end
end

function M.setEnabled(on)
    optSet("music_on", on)
    if on then
        if current then
            musicPlay(current.name, current.fade, current.loop)
        end
        musicVolume(savedVolume())
    else
        musicStop()
    end
end

-- Apply persisted volume to the engine. No track is started — the first
-- audio.play call does that (and is a no-op while music_on is false).
function M.applyPersisted()
    musicVolume(optGet("music_on", true) and savedVolume() or 0.0)
end

return M
