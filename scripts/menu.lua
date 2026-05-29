-- Main-menu / options / confirm-dialog scenes for SDLFun.
--
-- Each constructor returns a "scene" table (see engine/scene.lua) with
-- update/render/keydown/mousedown/etc. methods. The C side keeps
-- AppState + pendingAction; these scenes call the app_* bindings to
-- trigger NEW_GAME / CONTINUE / QUIT side-effects.
--
-- Built on engine.widget — every button is widget.button() with its
-- on_click capturing whatever C-side binding it should fire. Keyboard
-- nav (Up/Down/Left/Right, Tab, Return, Space) is dispatched through
-- widget.dispatch_keydown which handles spatial navigation between
-- enabled focusable widgets and fall-through to the focused widget's
-- own keydown.

local scene  = require "engine.scene"
local widget = require "engine.widget"

local M = {}

-- ---- Layout constants ---------------------------------------------------
-- Mirrors menu.h's old MENU_* constants. Distances are in virtual-canvas
-- units; SDLFun's canvas is 540 tall (set in main.cpp before including
-- ui.h).
local PAD       = 30
local BTN_W     = 190
local BTN_H     = 36
local BTN_GAP   = 7
local LOGO_H    = 48
local LOGO_W    = LOGO_H * 4    -- logo asset is 256x64, 4:1 aspect
local DLG_W     = 360
local DLG_H     = 170
local DLG_BTN_W = 110
local DLG_BTN_H = 36
local DLG_PAD   = 20

-- Text scales — multipliers of the font's native lineHeight.
-- Source fonts (per assets.lua aliases): orbitron lineHeight=40,
-- orbitron_small lineHeight=28.
local SCALE_BUTTON       = 0.85   -- button_font  (target ~24 vpx)
local SCALE_DIALOG_MSG   = 0.70   -- button_font  (target ~20 vpx)
local SCALE_DIALOG_TITLE = 0.90   -- dialog_title (target ~36 vpx)
local SCALE_MENU_TITLE   = 1.00   -- menu_title_font (target ~40 vpx)

-- Shared button styling — flat-color backgrounds keyed by state. Once
-- button art lands in assets.lua, swap to bg_up / bg_down / bg_disabled
-- (region names with slice metadata) on the same widget.button calls.
local BTN_BG_COLOR = {
    up       = { 0.14, 0.14, 0.22, 0.75 },
    focused  = { 0.55, 0.25, 0.85, 0.85 },
    down     = { 0.55, 0.25, 0.85, 1.00 },
    disabled = { 0.10, 0.10, 0.14, 0.75 },
}
local BTN_FG_COLOR = { 0.90, 0.90, 0.95, 1.0 }

-- One factory for every button in the menu — same look, just different
-- label / position / action.
local function make_menu_button(x, y, w, h, label, on_click)
    return widget.button({
        x = x, y = y, width = w, height = h,
        text       = label,
        font       = "button_font",
        text_scale = SCALE_BUTTON,
        text_align = ALIGN_MIDDLE + ALIGN_LEFT,
        text_color = BTN_FG_COLOR,
        bg_color   = BTN_BG_COLOR,
        on_click   = on_click,
    })
end

-- Walk a widget list delivering a mouse event by name. First widget to
-- claim the event wins. Used for mouseup; mousedown has its own helper
-- below because it also has to update focus.
local function dispatch_mouse(widgets, method, x, y, button)
    for _, w in ipairs(widgets) do
        if w[method](w, x, y, button) then return true end
    end
    return false
end

-- Mousedown dispatcher: tries each widget; the first that claims the
-- event also takes focus (matching standard click-to-focus). Returns
-- the new focused widget (or `current` if nothing focusable was hit).
local function dispatch_mousedown(widgets, current, x, y, button)
    for _, w in ipairs(widgets) do
        if w:mousedown(x, y, button) then
            if w.focusable and not w.disabled and w ~= current then
                if current then current.focused = false end
                w.focused = true
                return w
            end
            return current
        end
    end
    return current
end

-- Mousemove dispatcher: deliver to every widget regardless of hit-test
-- (a slider being dragged needs mousemove even when the cursor leaves
-- its track). Focus is NOT updated on hover — it moves on mousedown.
local function dispatch_mousemove(widgets, x, y)
    for _, w in ipairs(widgets) do
        if w.mousemove then w:mousemove(x, y) end
    end
end

-- ---- Main menu ---------------------------------------------------------

function M.main_menu()
    local mm = {}

    function mm:enter()
        local vw, vh = view_size()
        local hw, hh = vw * 0.5, vh * 0.5

        -- Logo at top-left.
        self.logo_x = -hw + PAD
        self.logo_y = -hh + PAD

        -- Button column under the logo.
        local x = -hw + PAD
        local y = -hh + PAD + LOGO_H + PAD
        local row = function(i) return y + (BTN_H + BTN_GAP) * i end

        self.widgets = {
            make_menu_button(x, row(0), BTN_W, BTN_H, "CONTINUE", function()
                app_continue()
            end),
            make_menu_button(x, row(1), BTN_W, BTN_H, "NEW GAME", function()
                if app_has_game() then
                    scene.push(M.confirm_dialog(
                        "NEW GAME",
                        "Start a new game? Progress will be lost.",
                        app_new_game))
                else
                    app_new_game()
                end
            end),
            make_menu_button(x, row(2), BTN_W, BTN_H, "OPTIONS", function()
                scene.push(M.options())
            end),
            make_menu_button(x, row(3), BTN_W, BTN_H, "EXIT", function()
                scene.push(M.confirm_dialog("EXIT", "Exit to system?", app_quit))
            end),
        }

        self:_layout()
        -- Initial focus: first enabled widget.
        self.focused_widget = widget.focus_next(self.widgets, nil)
        if self.focused_widget then self.focused_widget.focused = true end

        -- Title music starts on first menu entry; later re-enters
        -- crossfade smoothly.
        music_play("title", 1.0, true)
    end

    -- Toggle CONTINUE's enabled flag based on the C-side game pointer,
    -- and slide focus off it if it just became disabled.
    function mm:_layout()
        self.widgets[1].disabled = not app_has_game()
        if self.focused_widget and self.focused_widget.disabled then
            local next_w = widget.focus_next(self.widgets, self.focused_widget)
            if next_w and next_w ~= self.focused_widget then
                self.focused_widget.focused = false
                self.focused_widget = next_w
                self.focused_widget.focused = true
            end
        end
    end

    function mm:render()
        self:_layout()
        draw_bg("menu_bg")
        draw_region("logo", self.logo_x, self.logo_y, {
            scale_x = LOGO_W / 256.0,
            scale_y = LOGO_H / 64.0,
        })
        for _, w in ipairs(self.widgets) do w:draw() end
    end

    function mm:keydown(name)
        self.focused_widget = widget.dispatch_keydown(
            self.widgets, self.focused_widget, name)
    end

    function mm:mousemove(x, y)
        dispatch_mousemove(self.widgets, x, y)
    end

    function mm:mousedown(x, y, button)
        self.focused_widget = dispatch_mousedown(
            self.widgets, self.focused_widget, x, y, button)
    end

    function mm:mouseup(x, y, button)
        dispatch_mouse(self.widgets, "mouseup", x, y, button)
    end

    return mm
end

-- ---- Options -----------------------------------------------------------
--
-- Sample settings wired via opt_set / opt_get + the engine's
-- music_volume binding. Used as the dogfood scene for Checkbox + Slider
-- + Label.
--
-- - "Music" checkbox toggles persisted `music_on` and mutes/restores
--   the music volume immediately.
-- - "Master volume" slider sets persisted `music_vol` and applies it
--   live whenever Music is on.
--
-- Options are saved to sdlfun.dat when the user presses BACK / Esc /
-- Backspace; on_change runs only in-memory + applies the live audio
-- effect so dragging the slider doesn't thrash the disk.

function M.options()
    local opt = { transparent = true }   -- main menu renders behind us
    local vw, vh = view_size()
    opt.vw, opt.vh = vw, vh
    opt.title_y = -vh * 0.5 + PAD

    -- Persisted defaults — sane on first run.
    local music_on    = opt_get("music_on",    true)
    local music_vol   = opt_get("music_vol",   0.7)
    local player_name = opt_get("player_name", "")

    -- Layout: settings column starts below the title, centred.
    local COL_W = 320
    local COL_X = -COL_W * 0.5
    local row_y = opt.title_y + 60

    -- Forward reference so the checkbox's on_change can read the
    -- slider's current value.
    local music_slider

    local music_check = widget.checkbox{
        x = COL_X, y = row_y, width = COL_W, height = BTN_H,
        text  = "Music",
        font  = "button_font",
        scale = SCALE_BUTTON,
        color = BTN_FG_COLOR,
        value = music_on,
        on_change = function(self, v)
            opt_set("music_on", v)
            music_volume(v and music_slider.value or 0.0)
        end,
    }
    row_y = row_y + BTN_H + BTN_GAP

    local vol_label = widget.label{
        x = COL_X, y = row_y, width = COL_W, height = 22,
        text  = "Master volume",
        font  = "button_font",
        scale = SCALE_BUTTON,
        color = BTN_FG_COLOR,
        align = ALIGN_LEFT + ALIGN_MIDDLE,
    }
    row_y = row_y + 22 + 4

    music_slider = widget.slider{
        x = COL_X, y = row_y, width = COL_W, height = 18,
        min = 0.0, max = 1.0, value = music_vol, step = 0.05,
        on_change = function(self, v)
            opt_set("music_vol", v)
            if opt_get("music_on", true) then music_volume(v) end
        end,
    }
    row_y = row_y + 18 + BTN_GAP * 2

    local name_label = widget.label{
        x = COL_X, y = row_y, width = COL_W, height = 22,
        text  = "Player name",
        font  = "button_font",
        scale = SCALE_BUTTON,
        color = BTN_FG_COLOR,
        align = ALIGN_LEFT + ALIGN_MIDDLE,
    }
    row_y = row_y + 22 + 4

    local name_edit = widget.line_edit{
        x = COL_X, y = row_y, width = COL_W, height = 28,
        text = player_name,
        font = "button_font",
        scale = SCALE_BUTTON,
        color = BTN_FG_COLOR,
        bg_color = {
            enabled  = { 0.10, 0.10, 0.14, 0.85 },
            disabled = { 0.05, 0.05, 0.08, 0.6 },
        },
        padding     = { l = 8, r = 8, t = 4, b = 4 },
        max_length  = 24,
        placeholder = "Type your name",
        on_change   = function(self, t) opt_set("player_name", t) end,
    }
    row_y = row_y + 28 + BTN_GAP * 2

    local function commit_and_pop()
        opt_save()
        scene.pop()
    end

    local back = make_menu_button(-BTN_W * 0.5, row_y, BTN_W, BTN_H,
                                  "BACK", commit_and_pop)

    opt.widgets        = { music_check, vol_label, music_slider,
                           name_label, name_edit, back }
    opt.focused_widget = music_check
    music_check.focused = true

    function opt:render()
        draw_quad(-self.vw * 0.5, -self.vh * 0.5, self.vw, self.vh,
                  { color = { 0, 0, 0, 0.5 } })
        draw_text("OPTIONS", 0, self.title_y, {
            scale = SCALE_MENU_TITLE,
            font  = "menu_title_font",
            align = ALIGN_TOP + ALIGN_CENTER,
            color = { 1, 1, 1 },
        })
        for _, w in ipairs(self.widgets) do w:draw() end
    end

    function opt:update(dt)
        widget.dispatch_update(self.widgets, dt)
    end

    function opt:keydown(name)
        -- Esc commits + leaves. Backspace also commits — UNLESS the
        -- focused widget consumes it (LineEdit eats Backspace to delete
        -- a character). dispatch_keydown returns the same widget when
        -- the keypress was absorbed, so we differentiate by trying the
        -- widget first and only popping if the keydown bubbles back.
        if name == "escape" then
            commit_and_pop()
            return
        end
        if name == "backspace" then
            if self.focused_widget and self.focused_widget.keydown
               and self.focused_widget:keydown(name) then
                return       -- LineEdit consumed it
            end
            commit_and_pop()
            return
        end
        self.focused_widget = widget.dispatch_keydown(
            self.widgets, self.focused_widget, name)
    end

    function opt:textinput(ch)
        if self.focused_widget and self.focused_widget.textinput then
            self.focused_widget:textinput(ch)
        end
    end

    function opt:mousemove(x, y)
        dispatch_mousemove(self.widgets, x, y)
    end

    function opt:mousedown(x, y, button)
        self.focused_widget = dispatch_mousedown(
            self.widgets, self.focused_widget, x, y, button)
    end

    function opt:mouseup(x, y, button)
        dispatch_mouse(self.widgets, "mouseup", x, y, button)
    end

    return opt
end

-- ---- Confirm dialog ----------------------------------------------------
--
-- on_ok: a function invoked when OK is chosen (called AFTER the dialog
-- pops, so app_quit() / app_new_game() / etc. queue their pendingAction
-- and main.cpp picks it up the same frame).

function M.confirm_dialog(title, message, on_ok)
    local dlg = { transparent = true }
    dlg.title   = title
    dlg.message = message

    -- Dialog rect + button rects.
    local rx, ry = -DLG_W * 0.5, -DLG_H * 0.5
    local total  = DLG_BTN_W * 2 + 10
    local bx0    = rx + (DLG_W - total) * 0.5
    local by     = ry + DLG_H - DLG_PAD - DLG_BTN_H
    dlg.rect = { x = rx, y = ry, w = DLG_W, h = DLG_H }
    local vw, vh = view_size()
    dlg.vw, dlg.vh = vw, vh

    -- pop + (optional) fire pattern shared by OK / Cancel / Esc.
    local function pop_and_fire(choose_ok)
        scene.pop()
        if choose_ok and on_ok then on_ok() end
    end

    local ok = make_menu_button(bx0, by, DLG_BTN_W, DLG_BTN_H, "OK",
                                function() pop_and_fire(true) end)
    local cancel = make_menu_button(bx0 + DLG_BTN_W + 10, by, DLG_BTN_W, DLG_BTN_H,
                                    "CANCEL", function() pop_and_fire(false) end)
    -- Center buttons' text in their slightly-narrower frames.
    ok.text_align     = ALIGN_CENTER + ALIGN_MIDDLE
    cancel.text_align = ALIGN_CENTER + ALIGN_MIDDLE

    dlg.widgets        = { ok, cancel }
    dlg.focused_widget = cancel       -- Cancel is the safer default.
    cancel.focused     = true

    function dlg:render()
        draw_quad(-self.vw * 0.5, -self.vh * 0.5, self.vw, self.vh,
                  { color = { 0, 0, 0, 0.5 } })
        local r = self.rect
        -- Panel body. dialog_bg is a 32×32 tile asset in the original C
        -- menu; for now fill flat — wire the tile later by registering
        -- it as a slice-carrying region.
        draw_quad(r.x, r.y, r.w, r.h, { color = { 0.14, 0.14, 0.22, 1.0 } })
        widget.draw_outline(r.x, r.y, r.w, r.h, 2, { 0.9, 0.9, 1.0, 0.5 })
        draw_text(self.title, r.x + r.w * 0.5, r.y + DLG_PAD, {
            scale = SCALE_DIALOG_TITLE, font = "dialog_title",
            align = ALIGN_TOP + ALIGN_CENTER, color = { 1, 1, 1 },
        })
        draw_text(self.message, r.x + r.w * 0.5, r.y + r.h * 0.5, {
            scale = SCALE_DIALOG_MSG, font = "button_font",
            align = ALIGN_MIDDLE + ALIGN_CENTER, color = { 1, 1, 1 },
        })
        for _, w in ipairs(self.widgets) do w:draw() end
    end

    function dlg:keydown(name)
        if name == "escape" then
            pop_and_fire(false)
            return
        end
        self.focused_widget = widget.dispatch_keydown(
            self.widgets, self.focused_widget, name)
    end

    function dlg:mousemove(x, y)
        dispatch_mousemove(self.widgets, x, y)
    end

    function dlg:mousedown(x, y, button)
        self.focused_widget = dispatch_mousedown(
            self.widgets, self.focused_widget, x, y, button)
    end

    function dlg:mouseup(x, y, button)
        dispatch_mouse(self.widgets, "mouseup", x, y, button)
    end

    return dlg
end

return M
