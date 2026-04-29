-- Entry script for SDLFun. Engine calls scriptRunFile on this file once
-- at startup, then scriptCall("on_start") after the level + entities are
-- loaded. Define any of the engine hooks below; missing hooks are no-ops.
--
-- Engine bindings (v1):
--   ui_show_message(text [, seconds])         -- transient centered HUD message
--   snd_play(name)                            -- play a named buffer from assets
--   music_play(name [, fade [, loop]])        -- stream a track from assets.music
--   music_stop([fade])
--   music_volume(g)
--   ent_activate(target)                      -- trigger by entity name or group

function on_start()
    ui_show_message("Welcome to SDLFun", 3)
    snd_play("jump")
    music_play("ambient")                   -- crossfades over the menu's title track
end
