-- Asset manifest for SDLFun.
--
-- Lists the logical name → file path for every game asset. Loaded at
-- startup by scriptLoadAssets (script.h), which walks the tables and
-- populates the engine-side registries.
--
-- Name convention: short, lowercase, matching what game code / scripts
-- will pass to snd_play("...") etc. File paths are relative to the repo
-- root (same convention as every other asset in the engine).

return {
    sounds = {
        fire = "assets/sounds/fire.wav",
        step = "assets/sounds/step.wav",
        jump = "assets/sounds/jump.wav",
    },

    levels = {
        level1 = { obj = "test_level.obj", ent = "test_level.ent" },
        -- level2 = { obj = "...", ent = "..." },
    },
}
