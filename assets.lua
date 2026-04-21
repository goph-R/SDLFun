-- Asset manifest for SDLFun.
--
-- Lists logical name -> file path for every game asset. Loaded at
-- startup by scriptLoadAssets (script.h), which walks the tables and
-- populates the engine-side registries (SoundLibrary for `sounds`,
-- AssetRegistry for `models` and `textures`).
--
-- .ent files reference models/textures by the short names below
-- (mesh=office_desk, tex=wood1, iqm=mrfixit). Unknown names fall
-- through as raw paths so one-offs still work without registering.
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

    models = {
        mrfixit      = "assets/models/mrfixit.iqm",
        office_desk  = "assets/models/office-desk.obj",
        crt_monitor  = "assets/models/crt-monitor.obj",
        keyboard     = "assets/models/keyboard.obj",
        pc_tower     = "assets/models/midi-tower-pc-case.obj",
        office_chair = "assets/models/office-chair.obj",
        door         = "assets/models/door.obj",
        -- Enemies (see docs/min-enemy-assets.md).
        zombie       = "assets/models/zombie.iqm",
    },

    textures = {
        office = "assets/textures/office.bmp",
        wood1  = "assets/textures/wood1.bmp",
    },

    levels = {
        level1 = { obj = "assets/levels/test_level.obj", ent = "assets/levels/test_level.ent" },
        -- level2 = { obj = "...", ent = "..." },
    },
}
