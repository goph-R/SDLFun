-- Asset manifest for SDLFun.
--
-- Lists the logical name → file path for every game asset. Currently
-- authored only — the engine still hard-codes the sound list in main.cpp.
-- Once the Lua runtime is wired up (see PLAN_LUA.md) the engine will
-- `dofile` this and iterate the tables to build the SoundLibrary and the
-- level registry at startup.
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
