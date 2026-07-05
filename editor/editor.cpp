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
 * The lit level mesh (its own diffuse + baked lightmap) renders through the
 * engine path, and entity meshes resolve too: editLoadAssets (edit_assets.h)
 * runs assets.lua in a bare Lua state and registers its models + textures into
 * the AssetRegistry, so entLoadFile can map mesh=/iqm=/tex= names to files.
 * No UI/audio/script runtime is pulled in — just Lua.
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
#include <FL/Fl_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Button.H>
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
#include "edit_assets.h"     /* minimal assets.lua -> AssetRegistry (models+textures) */
#include "edit_mesh.h"       /* editor native mesh (verts/faces, 1 cm snap)          */
#include "edit_mesh_build.h" /* EditMesh -> ObjMesh for the engine renderer          */

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

    /* M0: the editor's own mesh (a demo cube for now) rendered through the
       new EditMesh -> ObjMesh -> engine path, alongside the loaded level. */
    EditMesh      emesh;
    ObjMesh       eobj;
    int           haveEmesh;

    /* Free-fly camera: eye position + yaw/pitch (radians). */
    float camX, camY, camZ;
    float yaw, pitch;
    int   lastX, lastY;

    EditorView(int X, int Y, int W, int H)
        : Fl_Gl_Window(X, Y, W, H),
          objPath("assets/levels/test_level.obj"),
          entPath("assets/levels/test_level.ent"),
          loaded(0), bootstrapped(0), haveEmesh(0),
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
            assetRegInit(&assetReg);
            editLoadAssets(&assetReg, "assets.lua");  /* resolve entity mesh/tex names */
            loaded = editLoadLevel(&scene, objPath, entPath, &assetReg);
            if (!loaded)
                conLogf("editor: failed to load %s / %s\n", objPath, entPath);

            /* M0: seed a hardcoded 2 m cube through the editor-mesh path. It
               borrows the loaded level's first material so it's textured with a
               real box-mapped diffuse; falls back to flat grey otherwise. */
            editMeshInit(&emesh);
            if (loaded && scene.level.numMaterials > 0) {
                emesh.mats[0] = scene.level.materials[0];
            } else {
                memset(&emesh.mats[0], 0, sizeof(Material));
                strcpy(emesh.mats[0].name, "editor_default");
                emesh.mats[0].tilingScale = 1.0f;
            }
            emesh.numMats = 1;
            editAddCube(&emesh, 0.0f, 1.0f, -4.0f, 2.0f, 2.0f, 2.0f, 0);
            objInit(&eobj);
            editMeshBuild(&emesh, &eobj);
            haveEmesh = (loaded != 0);   /* renderer needs scene.texCache valid */
            conLogf("editor: demo cube built (%d tris, %d sectors)\n",
                    eobj.numTris, eobj.numSectors);

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

        /* M0: draw the editor mesh through the same engine path. renderWorld
           leaves GL_LIGHTING enabled; the cube has no lightmap, so it takes the
           box-mapped diffuse + GL_LIGHT0 branch. */
        if (haveEmesh) {
            glEnable(GL_LIGHTING);
            renderLevelSectored(&eobj, &scene.texCache);
        }

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

/* Placeholder toolbar actions — just log which button was pressed for now.
   These are stubs to hang real editor commands off of later. */
static void toolbarCb(Fl_Widget *w, void *)
{
    conLogf("[toolbar] %s\n", w->label());
}

int main(int argc, char **argv)
{
    Fl::gl_visual(FL_RGB | FL_DEPTH | FL_DOUBLE);

    const int W = 1024, H = 768;
    const int TB = 30;                 /* toolbar height */

    Fl_Window *win = new Fl_Window(W, H, "SOOB Level Editor");
    win->begin();

    /* Toolbar strip across the top. Fixed height; the buttons are fake for
       now (they only log). A gap after "Save" and after "Scale" groups them
       into file / transform / playback clusters. */
    Fl_Group *toolbar = new Fl_Group(0, 0, W, TB);
    toolbar->box(FL_UP_BOX);
    {
        static const char *labels[] = {
            "New", "Open", "Save", "Select", "Move", "Rotate", "Scale", "Play"
        };
        const int n = (int)(sizeof(labels) / sizeof(labels[0]));
        int x = 4;
        for (int i = 0; i < n; i++) {
            if (i == 3 || i == 7) x += 8;          /* cluster gaps */
            Fl_Button *b = new Fl_Button(x, 3, 54, TB - 6, labels[i]);
            b->callback(toolbarCb);
            x += 54 + 2;
        }
    }
    toolbar->end();
    toolbar->resizable(NULL);          /* buttons stay put when the window resizes */

    /* GL viewport fills everything below the toolbar. */
    EditorView *view = new EditorView(0, TB, W, H - TB);
    if (argc > 1) view->objPath = argv[1];
    if (argc > 2) view->entPath = argv[2];

    win->end();
    win->resizable(view);              /* only the viewport grows/shrinks */

    conLogf("SOOB Level Editor — left-drag look, WASD move, Q/E down/up, wheel dolly\n");
    conLogf("Loading %s / %s (run from repo root)\n", view->objPath, view->entPath);

    win->show(argc, argv);
    view->take_focus();
    Fl::add_timeout(1.0 / 60.0, frameTimer, view);

    return Fl::run();
}
