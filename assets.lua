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
        office    = "assets/textures/office.png",
        wood1     = "assets/textures/wood1.png",
        -- Menu/UI textures.
        menu_bg    = "assets/textures/menu_bg.png",
        dialog_bg  = "assets/textures/dialog_bg.png",
        logo       = "assets/textures/logo.png",
        loading_bg = "assets/textures/loading_bg.png",
    },

    -- BMFont (AngelCode) bitmap fonts. Each entry points at a .fnt text file;
    -- the .fnt carries the atlas filename (a 32-bit RGBA PNG in the same directory)
    -- and per-glyph metrics. "default" is the font used when a uiText call
    -- passes no explicit font name — alias it to whichever font you want as
    -- the HUD baseline.
    --
    -- Loading the same .fnt under multiple logical names (e.g. button_font /
    -- orbitron_small) is cheap: uiFontLoad deduplicates by path and shares
    -- the atlas texture + glyph table between entries.
    fonts = {
        default         = "assets/fonts/orbitron_small.fnt",
        orbitron        = "assets/fonts/orbitron.fnt",
        orbitron_small  = "assets/fonts/orbitron_small.fnt",
        -- Menu-system aliases (same .fnt files, different logical names).
        button_font     = "assets/fonts/orbitron_small.fnt",
        dialog_title    = "assets/fonts/orbitron.fnt",
        menu_title_font = "assets/fonts/orbitron.fnt",
    },

    levels = {
        level1 = { obj = "assets/levels/test_level.obj", ent = "assets/levels/test_level.ent" },
        -- level2 = { obj = "...", ent = "..." },
    },
}
