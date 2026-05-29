-- Main-menu / options / confirm-dialog scenes for SDLFun.
--
-- Each constructor returns a "scene" table (see engine/scene.lua) with
-- update/render/keydown/mousedown/etc. methods. The C side keeps
-- AppState + pendingAction; these scenes call the app_* bindings to
-- trigger NEW_GAME / CONTINUE / QUIT side-effects.
--
-- Visual reproduction of the old menu.h C screens — minus the
-- screen-stack tagged union, plus the scene-stack composition. The
-- main_menu sits at the bottom; OPTIONS and confirm dialogs push as
-- transparent overlays so the main menu still renders behind them.

local scene = require "engine.scene"

local M = {}

-- ---- Layout constants ---------------------------------------------------
-- Mirrors menu.h's MENU_* constants. Distances are in virtual-canvas units;
-- SDLFun's canvas is 540 tall (set in main.cpp before including ui.h).
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

-- Text scales — values to multiply against the font's native lineHeight.
-- Tune on the dev box; engine bindings render `lineHeight * scale` tall.
-- Source fonts (per assets.lua aliases): orbitron lineHeight=40,
-- orbitron_small lineHeight=28.
local SCALE_BUTTON       = 0.85   -- button_font  (target ~24 vpx)
local SCALE_DIALOG_MSG   = 0.70   -- button_font  (target ~20 vpx)
local SCALE_DIALOG_TITLE = 0.90   -- dialog_title (target ~36 vpx)
local SCALE_MENU_TITLE   = 1.00   -- menu_title_font (target ~40 vpx)

-- ---- Button helpers ----------------------------------------------------

-- `action` is the logical command this button fires (e.g. "continue",
-- "exit", "back"). Storing it on the button table keeps the action and
-- its label/rect together — no parallel arrays to keep in sync.
local function make_button(label, x, y, w, h, enabled, action)
    return { label = label, x = x, y = y, w = w, h = h,
             enabled = enabled, action = action }
end

local function btn_contains(b, x, y)
    return x >= b.x and x < b.x + b.w and y >= b.y and y < b.y + b.h
end

-- Palette indexed by button state. Adding a new state means adding one
-- table entry, not extending an if-chain.
local BUTTON_COLORS = {
    disabled = { bg = { 0.10, 0.10, 0.14, 0.75 }, fg = { 0.55, 0.55, 0.60, 1.0 } },
    focused  = { bg = { 0.55, 0.25, 0.85, 0.85 }, fg = { 1.00, 1.00, 1.00, 1.0 } },
    idle     = { bg = { 0.14, 0.14, 0.22, 0.75 }, fg = { 0.90, 0.90, 0.95, 1.0 } },
}

local function draw_button(b, focused)
    local state
    if not b.enabled then state = "disabled"
    elseif focused      then state = "focused"
    else                     state = "idle"
    end
    local c = BUTTON_COLORS[state]
    draw_quad(b.x, b.y, b.w, b.h, { color = c.bg })
    draw_text(b.label, b.x + 12, b.y + b.h * 0.5, {
        scale = SCALE_BUTTON,
        font  = "button_font",
        align = ALIGN_MIDDLE + ALIGN_LEFT,
        color = c.fg,
    })
end

-- 1-px to N-px outline around a rect, drawn as four edge quads.
local function draw_outline(r, thickness, color)
    local opts = { color = color }
    draw_quad(r.x,                   r.y,                   r.w,       thickness, opts)
    draw_quad(r.x,                   r.y + r.h - thickness, r.w,       thickness, opts)
    draw_quad(r.x,                   r.y,                   thickness, r.h,       opts)
    draw_quad(r.x + r.w - thickness, r.y,                   thickness, r.h,       opts)
end

-- ---- Main menu ---------------------------------------------------------

function M.main_menu()
    local mm = {}

    -- Cycle focus index with wraparound, skipping disabled buttons. dir = ±1.
    local function focus_move(self, dir)
        local n = #self.buttons
        for _ = 1, n do
            self.focused = ((self.focused - 1 + dir + n) % n) + 1
            if self.buttons[self.focused].enabled then return end
        end
    end

    -- Update what changes per-frame: CONTINUE's enabled flag tracks
    -- app_has_game(), and focus slides off a button that just became
    -- disabled. Positions are fixed (SDL 1.2 OPENGL can't resize), so
    -- the button tables themselves stay around between renders.
    function mm:_layout()
        self.buttons[1].enabled = app_has_game()
        if not self.buttons[self.focused].enabled then
            for i, b in ipairs(self.buttons) do
                if b.enabled then self.focused = i; break end
            end
        end
    end

    function mm:enter()
        -- Cache the canvas size at entry — view_size() is a Lua→C binding
        -- and the canvas doesn't resize during a session.
        local vw, vh = view_size()
        local hw, hh = vw * 0.5, vh * 0.5

        -- Logo top-left, with PAD inset.
        self.logo_x = -hw + PAD
        self.logo_y = -hh + PAD

        -- Button column under the logo.
        local x = -hw + PAD
        local y = -hh + PAD + LOGO_H + PAD
        self.buttons = {
            make_button("CONTINUE", x, y,                          BTN_W, BTN_H, app_has_game(), "continue"),
            make_button("NEW GAME", x, y + (BTN_H + BTN_GAP),      BTN_W, BTN_H, true,           "new_game"),
            make_button("OPTIONS",  x, y + (BTN_H + BTN_GAP) * 2,  BTN_W, BTN_H, true,           "options"),
            make_button("EXIT",     x, y + (BTN_H + BTN_GAP) * 3,  BTN_W, BTN_H, true,           "exit"),
        }
        self.focused = 1
        self:_layout()   -- slide off CONTINUE if it starts disabled

        -- Title music starts on first menu entry. music_play with the
        -- same name on later re-renders is a no-op / smooth crossfade
        -- depending on the engine, so calling it once in enter is enough.
        music_play("title", 1.0, true)
    end

    function mm:render()
        self:_layout()
        draw_bg("menu_bg")
        draw_region("logo", self.logo_x, self.logo_y, {
            scale_x = LOGO_W / 256.0,    -- source 256x64; scale to LOGO_W x LOGO_H
            scale_y = LOGO_H / 64.0,
        })
        for i, b in ipairs(self.buttons) do
            draw_button(b, i == self.focused)
        end
    end

    function mm:_activate(idx)
        local action = self.buttons[idx].action
        if action == "continue" then
            app_continue()
        elseif action == "new_game" then
            if app_has_game() then
                scene.push(M.confirm_dialog(
                    "NEW GAME",
                    "Start a new game? Progress will be lost.",
                    app_new_game))
            else
                app_new_game()
            end
        elseif action == "options" then
            scene.push(M.options())
        elseif action == "exit" then
            scene.push(M.confirm_dialog("EXIT", "Exit to system?", app_quit))
        end
    end

    function mm:keydown(name)
        if name == "up" then
            focus_move(self, -1)
        elseif name == "down" then
            focus_move(self, 1)
        elseif name == "return" or name == "space" then
            local b = self.buttons[self.focused]
            if b and b.enabled then self:_activate(self.focused) end
        end
    end

    function mm:mousemove(x, y, dx, dy)
        for i, b in ipairs(self.buttons) do
            if b.enabled and btn_contains(b, x, y) then
                self.focused = i
                return
            end
        end
    end

    function mm:mousedown(x, y, button)
        if button ~= 1 then return end
        for i, b in ipairs(self.buttons) do
            if b.enabled and btn_contains(b, x, y) then
                self.focused = i
                self:_activate(i)
                return
            end
        end
    end

    return mm
end

-- ---- Options -----------------------------------------------------------

function M.options()
    local opt = { transparent = true }   -- main menu renders behind us
    opt.back = make_button("BACK", -BTN_W * 0.5, -BTN_H * 0.5, BTN_W, BTN_H, true, "back")
    opt.focused = 0
    -- Cache canvas size for the per-frame dim quad — view_size() doesn't
    -- change during a session.
    local vw, vh = view_size()
    opt.vw, opt.vh = vw, vh
    opt.title_y = -vh * 0.5 + PAD

    function opt:render()
        -- 50% dim over the main menu.
        draw_quad(-self.vw * 0.5, -self.vh * 0.5, self.vw, self.vh,
                  { color = { 0, 0, 0, 0.5 } })
        draw_text("OPTIONS", 0, self.title_y, {
            scale = SCALE_MENU_TITLE,
            font  = "menu_title_font",
            align = ALIGN_TOP + ALIGN_CENTER,
            color = { 1, 1, 1 },
        })
        draw_button(self.back, self.focused == 0)
    end

    function opt:keydown(name)
        if name == "return" or name == "space"
           or name == "escape" or name == "backspace" then
            scene.pop()
        end
    end

    function opt:mousemove(x, y)
        if btn_contains(self.back, x, y) then self.focused = 0 end
    end

    function opt:mousedown(x, y, button)
        if button == 1 and btn_contains(self.back, x, y) then
            scene.pop()
        end
    end

    return opt
end

-- ---- Confirm dialog ----------------------------------------------------
--
-- on_ok: a function invoked when OK is chosen (called BEFORE the dialog
-- pops, so app_quit() / app_new_game() / etc. can queue their pendingAction
-- and main.cpp picks it up next frame).

function M.confirm_dialog(title, message, on_ok)
    local dlg = { transparent = true }
    dlg.title   = title
    dlg.message = message
    dlg.on_ok   = on_ok
    dlg.focused = 1                       -- default = Cancel (safer)

    -- Pre-compute button rects (dialog is fixed-size, centered on origin).
    local rx, ry = -DLG_W * 0.5, -DLG_H * 0.5
    local total  = DLG_BTN_W * 2 + 10
    local bx0    = rx + (DLG_W - total) * 0.5
    local by     = ry + DLG_H - DLG_PAD - DLG_BTN_H
    dlg.rect   = { x = rx, y = ry, w = DLG_W, h = DLG_H }
    dlg.ok     = make_button("OK",     bx0,                    by, DLG_BTN_W, DLG_BTN_H, true)
    dlg.cancel = make_button("CANCEL", bx0 + DLG_BTN_W + 10,   by, DLG_BTN_W, DLG_BTN_H, true)
    -- Cache canvas size for the per-frame dim quad.
    local vw, vh = view_size()
    dlg.vw, dlg.vh = vw, vh

    function dlg:render()
        -- 50% dim over whatever's below.
        draw_quad(-self.vw * 0.5, -self.vh * 0.5, self.vw, self.vh,
                  { color = { 0, 0, 0, 0.5 } })
        -- Panel body. dialog_bg is a 32×32 tile asset in the original C
        -- menu; for now fill flat — wire the tile later by registering
        -- it as a region and switching to draw_region.
        local r = self.rect
        draw_quad(r.x, r.y, r.w, r.h, { color = { 0.14, 0.14, 0.22, 1.0 } })
        -- 2-px border.
        draw_outline(r, 2, { 0.9, 0.9, 1.0, 0.5 })
        -- Title + message.
        draw_text(self.title, r.x + r.w * 0.5, r.y + DLG_PAD, {
            scale = SCALE_DIALOG_TITLE, font = "dialog_title",
            align = ALIGN_TOP + ALIGN_CENTER, color = { 1, 1, 1 },
        })
        draw_text(self.message, r.x + r.w * 0.5, r.y + r.h * 0.5, {
            scale = SCALE_DIALOG_MSG, font = "button_font",
            align = ALIGN_MIDDLE + ALIGN_CENTER, color = { 1, 1, 1 },
        })
        -- Buttons.
        draw_button(self.ok,     self.focused == 0)
        draw_button(self.cancel, self.focused == 1)
    end

    function dlg:_fire()
        local choose_ok = (self.focused == 0)
        scene.pop()
        -- on_ok runs AFTER pop so the menu under us picks up any pendingAction
        -- this frame; the C side processes pendingAction once per frame after
        -- dispatching to scenes.
        if choose_ok and self.on_ok then self.on_ok() end
    end

    function dlg:keydown(name)
        if name == "left"  then self.focused = 0
        elseif name == "right" then self.focused = 1
        elseif name == "tab"   then self.focused = 1 - self.focused
        elseif name == "escape" then
            self.focused = 1
            self:_fire()
        elseif name == "return" or name == "space" then
            self:_fire()
        end
    end

    function dlg:mousemove(x, y)
        if btn_contains(self.ok,     x, y) then self.focused = 0; return end
        if btn_contains(self.cancel, x, y) then self.focused = 1; return end
    end

    function dlg:mousedown(x, y, button)
        if button ~= 1 then return end
        if btn_contains(self.ok, x, y) then
            self.focused = 0; self:_fire(); return
        end
        if btn_contains(self.cancel, x, y) then
            self.focused = 1; self:_fire(); return
        end
    end

    return dlg
end

return M
