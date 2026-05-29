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

-- Helper: walk a widget list dispatching a mouse event by name. Each
-- widget is queried in order; first one to claim the event wins.
local function dispatch_mouse(widgets, method, x, y, button)
    for _, w in ipairs(widgets) do
        if w[method](w, x, y, button) then return true end
    end
    return false
end

-- Helper: mouse-hover focus — when the cursor sits over an enabled
-- focusable widget, give it focus (clearing whichever widget had it
-- before). Matches the old menu's behavior where hovering a button
-- highlighted it.
local function hover_focus(widgets, current, x, y)
    for _, w in ipairs(widgets) do
        if w.focusable and not w.disabled and w:hit(x, y) then
            if current and current ~= w then current.focused = false end
            w.focused = true
            return w
        end
    end
    return current
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
        self.focused_widget = hover_focus(self.widgets, self.focused_widget, x, y)
    end

    function mm:mousedown(x, y, button)
        dispatch_mouse(self.widgets, "mousedown", x, y, button)
    end

    function mm:mouseup(x, y, button)
        dispatch_mouse(self.widgets, "mouseup", x, y, button)
    end

    return mm
end

-- ---- Options -----------------------------------------------------------

function M.options()
    local opt = { transparent = true }   -- main menu renders behind us
    local vw, vh = view_size()
    opt.vw, opt.vh = vw, vh
    opt.title_y = -vh * 0.5 + PAD

    local back = make_menu_button(-BTN_W * 0.5, -BTN_H * 0.5, BTN_W, BTN_H,
                                  "BACK", function() scene.pop() end)
    back.focused = true
    opt.widgets        = { back }
    opt.focused_widget = back

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

    function opt:keydown(name)
        -- Esc/Backspace are dialog-cancel-style shortcuts; always pop.
        if name == "escape" or name == "backspace" then
            scene.pop()
            return
        end
        self.focused_widget = widget.dispatch_keydown(
            self.widgets, self.focused_widget, name)
    end

    function opt:mousemove(x, y)
        self.focused_widget = hover_focus(self.widgets, self.focused_widget, x, y)
    end

    function opt:mousedown(x, y, button)
        dispatch_mouse(self.widgets, "mousedown", x, y, button)
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
        self.focused_widget = hover_focus(self.widgets, self.focused_widget, x, y)
    end

    function dlg:mousedown(x, y, button)
        dispatch_mouse(self.widgets, "mousedown", x, y, button)
    end

    function dlg:mouseup(x, y, button)
        dispatch_mouse(self.widgets, "mouseup", x, y, button)
    end

    return dlg
end

return M
