/*
 * SOOB Level Editor — skeleton
 * ============================
 *
 * A native Win98-capable level editor for the SOOB / SDLFun engine, built on
 * FLTK 1.3 (fixed-function GL, no C++11, no shaders — same envelope as the
 * game). It renders levels through the EXACT engine code the game ships:
 *
 *     editLoadLevel()  (edit_load.h)     -> build a Game with just the
 *                                            renderable state (no physics/
 *                                            nav/scripts/flashlight)
 *     renderWorld()    (render_world.h)  -> lit level + entities, lightmaps
 *                                            and all, from a free-fly Camera
 *     render_level.h                     -> the shared GL draw path itself
 *
 * On top of that it draws an editor-only overlay (ground grid + origin axis
 * gizmo) — the seed for gizmos / selection / entity handles later.
 *
 * This is a SKELETON: it boots an EMPTY AssetRegistry, so the lit level mesh
 * (its own diffuse + baked lightmap) renders fully, but entity *meshes* won't
 * resolve until the asset pipeline (scriptLoadAssets from assets.lua) is
 * wired in — the clear next step. Entity transforms still parse, so entity
 * markers could be drawn from the overlay meanwhile.
 *
 * Run from the repo root (assets are relative-pathed, exactly like the game):
 *     ./soob_editor [level.obj] [level.ent]
 * defaults to assets/levels/test_level.{obj,ent}.
 *
 * Controls: left-drag = look, WASD = move, Q/E = down/up, wheel = dolly.
 */

#ifdef _WIN32
#include <windows.h>          /* wglGetProcAddress + APIENTRY, ahead of GL */
#endif

#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/gl.h>

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cmath>

/* The engine's header-only modules all call conLogf() (the game routes it to
   its dev console). The editor has no console, so provide a plain stdout sink
   BEFORE including any engine header. */
static void conLogf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}

/* Engine modules — struct field types first, then the shared render/load
   headers. Deliberately NOT included: ui.h, sound.h, music.h, script.h — the
   game.h/game_session.h split keeps those out of the editor's TU. */
#include "obj_loader.h"
#include "texture.h"          /* SOOB-Core */
#include "iqm.h"
#include "asset_registry.h"   /* SOOB-Core */
#include "entity.h"
#include "flashlight.h"
#include "physics.h"
#include "nav.h"
#include "path.h"
#include "game.h"             /* struct Game only (session lifecycle omitted) */
#include "render_level.h"
#include "render_world.h"
#include "edit_load.h"

/* GL proc loader for initMultitexture(). On Win98/MinGW the ARB multitexture
   entry points are resolved at runtime; on Linux they're linked directly and
   the loader is ignored (pass NULL). wglGetProcAddress returns PROC (__stdcall,
   not void*), so wrap it to match the GLProcLoader signature. */
#ifdef _WIN32
static void *editGLGetProc(const char *name)
{
    return (void *)wglGetProcAddress(name);
}
#endif

class EditorView : public Fl_Gl_Window {
public:
    const char   *objPath;
    const char   *entPath;

    Game          scene;
    AssetRegistry assetReg;
    int           loaded;      /* level loaded OK */
    int           bootstrapped; /* GL init + load done (needs live context) */

    /* Free-fly camera: eye position + yaw/pitch (radians). */
    float camX, camY, camZ;
    float yaw, pitch;
    int   lastX, lastY;

    EditorView(int W, int H, const char *L)
        : Fl_Gl_Window(W, H, L),
          objPath("assets/levels/test_level.obj"),
          entPath("assets/levels/test_level.ent"),
          loaded(0), bootstrapped(0),
          camX(0.0f), camY(2.0f), camZ(6.0f),
          yaw(-1.5708f), pitch(-0.15f), lastX(0), lastY(0)
    {
        mode(FL_RGB | FL_DEPTH | FL_DOUBLE);
    }

    /* eye + look-at target from the free-fly camera state. */
    void computeCamera(Camera *cam)
    {
        cam->rollDeg = 0.0f;
        cam->eye.x = camX; cam->eye.y = camY; cam->eye.z = camZ;
        float cp = cosf(pitch);
        cam->at.x = camX + cosf(yaw) * cp;
        cam->at.y = camY + sinf(pitch);
        cam->at.z = camZ + sinf(yaw) * cp;
    }

    void initGL()
    {
#ifdef _WIN32
        initMultitexture(editGLGetProc);
#else
        initMultitexture(NULL);   /* Linux: uses linked ARB symbols */
#endif
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);

        /* Fallback GL_LIGHT0 (matches the game's init) — lightmapped sectors
           disable lighting themselves; this lights entities and any
           un-lightmapped geometry. renderWorld sets its POSITION each frame. */
        glEnable(GL_LIGHT0);
        GLfloat amb[] = { 0.30f, 0.30f, 0.30f, 1.0f };
        GLfloat dif[] = { 0.85f, 0.85f, 0.85f, 1.0f };
        glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    }

    void draw()
    {
        /* First live frame: context is current, so GL setup + level load
           (which may upload textures) are safe to run here rather than in the
           constructor. */
        if (!bootstrapped) {
            initGL();
            assetRegInit(&assetReg);     /* empty registry — skeleton */
            loaded = editLoadLevel(&scene, objPath, entPath, &assetReg);
            if (!loaded)
                conLogf("editor: failed to load %s / %s\n", objPath, entPath);
            bootstrapped = 1;
        }

        int pw = w() > 0 ? w() : 1;
        int ph = h() > 0 ? h() : 1;
        glViewport(0, 0, pw, ph);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glSetPerspective(70.0f, (float)pw / (float)ph, 0.05f, 500.0f);

        glClearColor(0.16f, 0.17f, 0.21f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        Camera cam;
        computeCamera(&cam);

        /* Set the view once so the overlay draws in world space even before a
           level is loaded; renderWorld re-establishes the same matrix. */
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glLookAt(cam.eye, cam.at);

        if (loaded)
            renderWorld(&scene, cam);

        drawOverlay();
    }

    /* Editor-only chrome — never touches the engine renderer. Drawn in world
       space (modelview is still the view matrix here). */
    void drawOverlay()
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);

        /* Ground grid on the XZ plane (depth-tested so it sits under geo). */
        glColor3f(0.30f, 0.30f, 0.36f);
        glBegin(GL_LINES);
        const int N = 20;
        for (int i = -N; i <= N; i++) {
            glVertex3f((float)i, 0.0f, (float)-N);
            glVertex3f((float)i, 0.0f, (float) N);
            glVertex3f((float)-N, 0.0f, (float)i);
            glVertex3f((float) N, 0.0f, (float)i);
        }
        glEnd();

        /* Origin axis gizmo: X red, Y green, Z blue. Drawn on top (no depth)
           so it's always visible as a reference. */
        glDisable(GL_DEPTH_TEST);
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        glColor3f(1.0f, 0.25f, 0.25f); glVertex3f(0,0,0); glVertex3f(2.0f,0,0);
        glColor3f(0.25f, 1.0f, 0.25f); glVertex3f(0,0,0); glVertex3f(0,2.0f,0);
        glColor3f(0.35f, 0.5f, 1.0f);  glVertex3f(0,0,0); glVertex3f(0,0,2.0f);
        glEnd();
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);

        glEnable(GL_LIGHTING);
    }

    /* Poll held movement keys (called from the frame timer). Returns 1 if the
       camera moved (so the caller can redraw). */
    int moveFromKeys()
    {
        const float speed = 0.15f;
        Camera cam;
        computeCamera(&cam);

        float fx = cam.at.x - cam.eye.x;
        float fy = cam.at.y - cam.eye.y;
        float fz = cam.at.z - cam.eye.z;
        float fl = sqrtf(fx*fx + fy*fy + fz*fz);
        if (fl > 1e-4f) { fx /= fl; fy /= fl; fz /= fl; }

        /* right = normalize(cross(forward, up(0,1,0))) = normalize(-fz, 0, fx) */
        float rx = -fz, rz = fx;
        float rl = sqrtf(rx*rx + rz*rz);
        if (rl > 1e-4f) { rx /= rl; rz /= rl; }

        int moved = 0;
        if (Fl::get_key('w')) { camX += fx*speed; camY += fy*speed; camZ += fz*speed; moved = 1; }
        if (Fl::get_key('s')) { camX -= fx*speed; camY -= fy*speed; camZ -= fz*speed; moved = 1; }
        if (Fl::get_key('d')) { camX += rx*speed; camZ += rz*speed; moved = 1; }
        if (Fl::get_key('a')) { camX -= rx*speed; camZ -= rz*speed; moved = 1; }
        if (Fl::get_key('e')) { camY += speed; moved = 1; }
        if (Fl::get_key('q')) { camY -= speed; moved = 1; }
        return moved;
    }

    int handle(int e)
    {
        switch (e) {
        case FL_PUSH:
            lastX = Fl::event_x();
            lastY = Fl::event_y();
            take_focus();
            return 1;
        case FL_DRAG: {
            int dx = Fl::event_x() - lastX;
            int dy = Fl::event_y() - lastY;
            lastX = Fl::event_x();
            lastY = Fl::event_y();
            yaw   += dx * 0.005f;
            pitch -= dy * 0.005f;
            if (pitch >  1.55f) pitch =  1.55f;
            if (pitch < -1.55f) pitch = -1.55f;
            redraw();
            return 1;
        }
        case FL_MOUSEWHEEL: {
            Camera cam;
            computeCamera(&cam);
            float fx = cam.at.x - cam.eye.x;
            float fy = cam.at.y - cam.eye.y;
            float fz = cam.at.z - cam.eye.z;
            float step = -Fl::event_dy() * 0.6f;
            camX += fx*step; camY += fy*step; camZ += fz*step;
            redraw();
            return 1;
        }
        case FL_FOCUS:
        case FL_UNFOCUS:
            return 1;               /* keep receiving keyboard focus */
        case FL_KEYDOWN:
        case FL_KEYUP:
            return 1;               /* movement handled by the frame timer */
        }
        return Fl_Gl_Window::handle(e);
    }
};

/* ~60 Hz frame pump: poll movement keys, redraw when the camera moves. */
static void frameTimer(void *v)
{
    EditorView *view = (EditorView *)v;
    if (view->moveFromKeys())
        view->redraw();
    Fl::repeat_timeout(1.0 / 60.0, frameTimer, v);
}

int main(int argc, char **argv)
{
    Fl::gl_visual(FL_RGB | FL_DEPTH | FL_DOUBLE);

    EditorView *view = new EditorView(1024, 768, "SOOB Level Editor");
    if (argc > 1) view->objPath = argv[1];
    if (argc > 2) view->entPath = argv[2];
    view->resizable(view);
    view->end();

    conLogf("SOOB Level Editor — left-drag look, WASD move, Q/E down/up, wheel dolly\n");
    conLogf("Loading %s / %s (run from repo root)\n", view->objPath, view->entPath);

    view->show(argc, argv);
    view->take_focus();
    Fl::add_timeout(1.0 / 60.0, frameTimer, view);

    return Fl::run();
}
