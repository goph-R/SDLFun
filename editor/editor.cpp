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
 * Controls: right-drag = look, WASD = move, PgUp/PgDn = up/down, wheel = dolly,
 * left-click = select, Shift+left-click = add/toggle, 1/2/3 = vertex/edge/face,
 * G = grab/move (X/Y/Z axis-lock, click/Enter confirm, Esc cancel),
 * E = extrude faces, F = make face (3-4 verts), X/Del = delete,
 * Ctrl+Z / Ctrl+Y = undo / redo.
 * Menus: Add (Cube/Plane), Mesh (Extrude / Make Face / Flip Normals / Delete).
 */

#ifdef _WIN32
#include <windows.h>          /* wglGetProcAddress + APIENTRY, ahead of GL */
#endif

#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Pixmap.H>
#include <FL/fl_ask.H>
#include <FL/gl.h>

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cmath>

/* Toolbar select-mode icons — embedded XPM pixmaps. Draw them in
   editor/icons/*.xpm (keep the array names). */
#include "icons/mode_vert.xpm"
#include "icons/mode_edge.xpm"
#include "icons/mode_face.xpm"

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
#include "edit_select.h"     /* vert/edge/face selection state                       */
#include "edit_pick.h"       /* screen-space + ray picking (no GLU)                  */
#include "edit_undo.h"       /* whole-mesh snapshot undo/redo                        */
#include "edit_ops.h"        /* extrude                                              */

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

    EditSelection sel;          /* M1: vert/edge/face selection */
    int           pushX, pushY; /* mouse-down point, for click-vs-drag */

    /* Toolbar mode buttons (radio); kept in sync with sel.mode both ways. */
    Fl_Button    *bVert, *bEdge, *bFace;

    EditHistory   hist;         /* M2: snapshot undo/redo */
    Fl_Menu_Bar  *menuBar;      /* Edit menu, for greying out Undo/Redo */
    int           undoIdx, redoIdx;

    /* M5 property-panel widgets (material + tiling of the selected faces). */
    Fl_Choice      *matChoice;
    Fl_Input       *diffuseInput;
    Fl_Value_Input *scaleInput, *offXInput, *offYInput;

    /* M2 grab (modal move): active while `grabbing`. The affected verts and
       their pre-grab positions are captured at start; the mouse delta
       (optionally axis-locked) moves them, snapped to 1 cm. */
    int   grabbing, grabAxis;   /* grabAxis: -1 none, 0/1/2 = X/Y/Z */
    int   grabAnchorX, grabAnchorY, grabCurX, grabCurY;
    Vec3  grabCentroid;
    int  *grabVerts; Vec3 *grabOrig; int nGrab;
    int   suppressRelease;      /* skip the select on a grab-confirm click */
    int   grabFromExtrude;      /* grab launched by extrude (cancel = full undo) */

    /* Free-fly camera: eye position + yaw/pitch (radians). */
    float camX, camY, camZ;
    float yaw, pitch;
    int   lastX, lastY;

    EditorView(int X, int Y, int W, int H)
        : Fl_Gl_Window(X, Y, W, H),
          objPath("assets/levels/test_level.obj"),
          entPath("assets/levels/test_level.ent"),
          loaded(0), bootstrapped(0), haveEmesh(0), pushX(0), pushY(0),
          bVert(0), bEdge(0), bFace(0),
          menuBar(0), undoIdx(-1), redoIdx(-1),
          matChoice(0), diffuseInput(0), scaleInput(0), offXInput(0), offYInput(0),
          grabbing(0), grabAxis(-1),
          grabAnchorX(0), grabAnchorY(0), grabCurX(0), grabCurY(0),
          grabVerts(0), grabOrig(0), nGrab(0), suppressRelease(0), grabFromExtrude(0),
          camX(0.0f), camY(2.0f), camZ(6.0f),
          yaw(-1.5708f), pitch(-0.15f), lastX(0), lastY(0)
    {
        mode(FL_RGB | FL_DEPTH | FL_DOUBLE);
        editHistoryInit(&hist);
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
            editSelInit(&sel, &emesh);   /* M1: selection + derived edge list */
            haveEmesh = (loaded != 0);   /* renderer needs scene.texCache valid */
            rebuildMatChoice();          /* M5: fill the material dropdown */
            refreshPanel();
            conLogf("editor: demo cube built (%d tris, %d sectors, %d edges)\n",
                    eobj.numTris, eobj.numSectors, sel.numEdges);

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
            drawSelectionOverlay();
        }

        drawOverlay();
    }

    /* M1: draw the edit-mesh wireframe + the current selection. Everything is
       depth-tested so hidden parts stay hidden (matching the occlusion-aware
       picking); a small glDepthRange bias pulls the overlay just in front of
       the coincident surfaces to avoid z-fighting. Colours: dark wire, orange
       (1.0, 0.6, 0.1) selection. */
    void drawSelectionOverlay()
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);

        /* Bias the overlay slightly toward the camera (0.999 vs 1.0) so lines,
           points and fills sit on top of the coincident geometry, while still
           being occluded by clearly-nearer faces. */
        glDepthRange(0.0, 0.999);

        /* All edges, thin + dark. */
        glLineWidth(1.0f);
        glColor3f(0.05f, 0.05f, 0.05f);
        glBegin(GL_LINES);
        for (int i = 0; i < sel.numEdges; i++) {
            Vec3 *a = &emesh.verts[sel.edges[i].a].pos;
            Vec3 *b = &emesh.verts[sel.edges[i].b].pos;
            glVertex3f(a->x, a->y, a->z);
            glVertex3f(b->x, b->y, b->z);
        }
        glEnd();

        /* Unselected verts (vertex mode only), depth-tested. */
        if (sel.mode == SEL_VERT) {
            glPointSize(4.0f);
            glColor3f(0.1f, 0.1f, 0.1f);
            glBegin(GL_POINTS);
            for (int i = 0; i < emesh.numVerts; i++) {
                if (sel.vertSel[i]) continue;
                Vec3 *p = &emesh.verts[i].pos;
                glVertex3f(p->x, p->y, p->z);
            }
            glEnd();
        }

        /* Selection highlight — depth-tested (hidden parts stay hidden). */
        if (sel.mode == SEL_FACE) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_CULL_FACE);
            glColor4f(1.0f, 0.6f, 0.1f, 0.4f);
            glBegin(GL_TRIANGLES);
            for (int i = 0; i < emesh.numFaces; i++) {
                if (!sel.faceSel[i]) continue;
                EditFace *f = &emesh.faces[i];
                Vec3 *v0 = &emesh.verts[f->v[0]].pos;
                Vec3 *v1 = &emesh.verts[f->v[1]].pos;
                Vec3 *v2 = &emesh.verts[f->v[2]].pos;
                glVertex3f(v0->x, v0->y, v0->z);
                glVertex3f(v1->x, v1->y, v1->z);
                glVertex3f(v2->x, v2->y, v2->z);
                if (f->nv == 4) {
                    Vec3 *v3 = &emesh.verts[f->v[3]].pos;
                    glVertex3f(v0->x, v0->y, v0->z);
                    glVertex3f(v2->x, v2->y, v2->z);
                    glVertex3f(v3->x, v3->y, v3->z);
                }
            }
            glEnd();
            glEnable(GL_CULL_FACE);
            glDisable(GL_BLEND);
        } else if (sel.mode == SEL_EDGE) {
            glLineWidth(3.0f);
            glColor3f(1.0f, 0.6f, 0.1f);
            glBegin(GL_LINES);
            for (int i = 0; i < sel.numEdges; i++) {
                if (!sel.edgeSel[i]) continue;
                Vec3 *a = &emesh.verts[sel.edges[i].a].pos;
                Vec3 *b = &emesh.verts[sel.edges[i].b].pos;
                glVertex3f(a->x, a->y, a->z);
                glVertex3f(b->x, b->y, b->z);
            }
            glEnd();
            glLineWidth(1.0f);
        } else { /* SEL_VERT */
            glPointSize(8.0f);
            glColor3f(1.0f, 0.6f, 0.1f);
            glBegin(GL_POINTS);
            for (int i = 0; i < emesh.numVerts; i++) {
                if (!sel.vertSel[i]) continue;
                Vec3 *p = &emesh.verts[i].pos;
                glVertex3f(p->x, p->y, p->z);
            }
            glEnd();
            glPointSize(1.0f);
        }

        glDepthRange(0.0, 1.0);
        glEnable(GL_LIGHTING);
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
        /* PgUp/PgDn raise/lower (Q/E freed — E is now extrude). */
        if (Fl::get_key(FL_Page_Up))   { camY += speed; moved = 1; }
        if (Fl::get_key(FL_Page_Down)) { camY -= speed; moved = 1; }
        return moved;
    }

    /* Fill a PickCam from the current free-fly camera + viewport, matching the
       70° vertical FOV that draw()'s glSetPerspective uses. */
    void buildPickCam(PickCam *pc)
    {
        Camera cam;
        computeCamera(&cam);
        Vec3 fwd   = editV3Norm(editV3Sub(cam.at, cam.eye));
        Vec3 right = editV3Norm(editV3Cross(fwd, editV3(0.0f, 1.0f, 0.0f)));
        Vec3 up    = editV3Cross(right, fwd);
        pc->eye = cam.eye; pc->forward = fwd; pc->right = right; pc->up = up;
        pc->tanHalfFov = tanf((70.0f * 3.14159265f / 180.0f) * 0.5f);
        int pw = w() > 0 ? w() : 1;
        int ph = h() > 0 ? h() : 1;
        pc->aspect = (float)pw / (float)ph;
        pc->vpW = pw; pc->vpH = ph;
    }

    /* Pick at viewport-local (mx,my) in the active mode. additive (Shift)
       toggles the hit element and keeps the rest; a plain click replaces the
       selection (and clears it on a miss). */
    void doPick(float mx, float my, int additive)
    {
        PickCam pc;
        buildPickCam(&pc);
        if (sel.mode == SEL_VERT) {
            int idx = editPickVertex(&pc, &emesh, mx, my, 8.0f);
            if (!additive) editSelClearActive(&sel);
            if (idx >= 0) sel.vertSel[idx] = additive ? !sel.vertSel[idx] : 1;
        } else if (sel.mode == SEL_EDGE) {
            int idx = editPickEdge(&pc, &emesh, sel.edges, sel.numEdges, mx, my, 8.0f);
            if (!additive) editSelClearActive(&sel);
            if (idx >= 0) sel.edgeSel[idx] = additive ? !sel.edgeSel[idx] : 1;
        } else {
            int idx = editPickFace(&pc, &emesh, mx, my);
            if (!additive) editSelClearActive(&sel);
            if (idx >= 0) sel.faceSel[idx] = additive ? !sel.faceSel[idx] : 1;
        }
        refreshPanel();
        redraw();
    }

    /* Set the selection mode and keep the toolbar radio buttons in sync — used
       by both the 1/2/3 keys and the toolbar button callbacks. Setting a radio
       button's value() doesn't clear its siblings, so set all three. */
    void applyMode(EditSelMode m)
    {
        sel.mode = m;
        if (bVert) bVert->value(m == SEL_VERT);
        if (bEdge) bEdge->value(m == SEL_EDGE);
        if (bFace) bFace->value(m == SEL_FACE);
        redraw();
    }

    /* ---- M2: modal grab (move) ------------------------------------------ */

    Vec3 grabAxisVec()
    {
        return editV3(grabAxis == 0 ? 1.0f : 0.0f,
                      grabAxis == 1 ? 1.0f : 0.0f,
                      grabAxis == 2 ? 1.0f : 0.0f);
    }

    /* Collect the unique verts the current selection moves, with their pre-grab
       positions + centroid. Returns the count (0 = nothing to grab). */
    int grabGather()
    {
        int nV = emesh.numVerts;
        unsigned char *mark = (unsigned char *)calloc(nV > 0 ? nV : 1, 1);
        int i, j;
        if (sel.mode == SEL_VERT) {
            for (i = 0; i < nV; i++) if (sel.vertSel[i]) mark[i] = 1;
        } else if (sel.mode == SEL_EDGE) {
            for (i = 0; i < sel.numEdges; i++)
                if (sel.edgeSel[i]) { mark[sel.edges[i].a] = 1; mark[sel.edges[i].b] = 1; }
        } else {
            for (i = 0; i < emesh.numFaces; i++)
                if (sel.faceSel[i])
                    for (j = 0; j < emesh.faces[i].nv; j++) mark[emesh.faces[i].v[j]] = 1;
        }
        int n = 0;
        for (i = 0; i < nV; i++) if (mark[i]) n++;
        if (n == 0) { free(mark); return 0; }

        grabVerts = (int *)malloc(n * sizeof(int));
        grabOrig  = (Vec3 *)malloc(n * sizeof(Vec3));
        nGrab = 0;
        Vec3 c = editV3(0, 0, 0);
        for (i = 0; i < nV; i++) {
            if (!mark[i]) continue;
            grabVerts[nGrab] = i;
            grabOrig[nGrab]  = emesh.verts[i].pos;
            c = editV3Add(c, emesh.verts[i].pos);
            nGrab++;
        }
        grabCentroid = editV3Scale(c, 1.0f / (float)nGrab);
        free(mark);
        return nGrab;
    }

    void grabEnd()
    {
        free(grabVerts); free(grabOrig);
        grabVerts = NULL; grabOrig = NULL;
        nGrab = 0; grabbing = 0; grabAxis = -1; grabFromExtrude = 0;
    }

    /* Anchor to the mouse and arm the modal move. The caller must have already
       gathered the verts (grabGather) and pushed any undo snapshot. */
    void grabBeginCommon()
    {
        grabAxis = -1;
        grabAnchorX = grabCurX = Fl::event_x();
        grabAnchorY = grabCurY = Fl::event_y();
        grabbing = 1;
        updateMenuEnabled();
    }

    void grabStart()
    {
        if (grabbing || !haveEmesh) return;
        if (grabGather() == 0) { conLogf("grab: nothing selected\n"); return; }
        editHistoryPush(&hist, &emesh);          /* pre-grab restore point */
        grabFromExtrude = 0;
        grabBeginCommon();
        conLogf("grab: %d vert(s)\n", nGrab);
    }

    /* Recompute moved positions from originals + the current mouse delta. The
       delta lives in the view plane through the selection centroid (screen
       pixels -> world units at that depth); an axis lock projects onto X/Y/Z. */
    void grabApply()
    {
        PickCam pc; buildPickCam(&pc);
        float d = editDepth(&pc, grabCentroid);
        if (d < 0.05f) d = 0.05f;
        float wpp = (2.0f * d * pc.tanHalfFov) / (float)pc.vpH;
        float dxp = (float)(grabCurX - grabAnchorX);
        float dyp = (float)(grabCurY - grabAnchorY);
        Vec3 delta = editV3Add(editV3Scale(pc.right, dxp * wpp),
                               editV3Scale(pc.up,   -dyp * wpp));
        if (grabAxis >= 0) {
            Vec3 ax = grabAxisVec();
            delta = editV3Scale(ax, editV3Dot(delta, ax));
        }
        for (int k = 0; k < nGrab; k++) {
            Vec3 np = editV3Add(grabOrig[k], delta);
            emesh.verts[grabVerts[k]].pos.x = editSnap(np.x);
            emesh.verts[grabVerts[k]].pos.y = editSnap(np.y);
            emesh.verts[grabVerts[k]].pos.z = editSnap(np.z);
        }
        editMeshBuild(&emesh, &eobj);
        redraw();
    }

    void grabUpdate(int mx, int my) { grabCurX = mx; grabCurY = my; grabApply(); }

    void grabConfirm() { grabEnd(); redraw(); }   /* snapshot already on the stack */

    void grabCancel()
    {
        if (grabFromExtrude) {
            /* undo the whole extrude+move as one step (removes the new geometry) */
            editHistoryPopRestore(&hist, &emesh);
            grabEnd();
            afterTopologyEdit();
            return;
        }
        for (int k = 0; k < nGrab; k++)
            emesh.verts[grabVerts[k]].pos = grabOrig[k];
        editMeshBuild(&emesh, &eobj);
        editHistoryDropUndoTop(&hist);            /* back to pre-grab: drop it */
        grabEnd();
        updateMenuEnabled();
        redraw();
    }

    /* After an undo/redo restores the mesh: rebuild the render mesh, and only
       re-init the selection if the vert/face counts changed (topology edit);
       for a grab (counts stable) the selection stays valid. */
    void afterRestore()
    {
        editMeshBuild(&emesh, &eobj);
        if (sel.numVerts != emesh.numVerts || sel.numFaces != emesh.numFaces) {
            EditSelMode prev = sel.mode;
            editSelFree(&sel);
            editSelInit(&sel, &emesh);
            applyMode(prev);
        }
        refreshPanel();
        redraw();
    }

    /* Grey out Edit/Undo or Edit/Redo when its stack is empty. */
    void updateMenuEnabled()
    {
        if (!menuBar || undoIdx < 0 || redoIdx < 0) return;
        Fl_Menu_Item *m = (Fl_Menu_Item *)menuBar->menu();
        if (hist.nUndo > 0) m[undoIdx].activate(); else m[undoIdx].deactivate();
        if (hist.nRedo > 0) m[redoIdx].activate(); else m[redoIdx].deactivate();
    }

    /* Undo/redo entry points shared by the Ctrl+Z/Y keys and the Edit menu.
       Disabled mid-grab (finish or cancel the grab first). */
    void doUndo() { if (!grabbing && editHistoryUndo(&hist, &emesh)) afterRestore(); updateMenuEnabled(); }
    void doRedo() { if (!grabbing && editHistoryRedo(&hist, &emesh)) afterRestore(); updateMenuEnabled(); }

    /* ---- M3: make-face / flip / delete / add-primitive ------------------ */

    /* World point ~5 m in front of the camera (snapped) — where new primitives
       are dropped. */
    Vec3 focusPoint()
    {
        Camera cam; computeCamera(&cam);
        Vec3 fwd = editV3Norm(editV3Sub(cam.at, cam.eye));
        Vec3 p = editV3Add(cam.eye, editV3Scale(fwd, 5.0f));
        return editV3(editSnap(p.x), editSnap(p.y), editSnap(p.z));
    }

    /* Rebuild the render mesh, reset the selection (topology changed), and
       refresh the Undo/Redo menu state. */
    void afterTopologyEdit()
    {
        editMeshBuild(&emesh, &eobj);
        EditSelMode prev = sel.mode;
        editSelFree(&sel);
        editSelInit(&sel, &emesh);
        applyMode(prev);
        updateMenuEnabled();
        refreshPanel();
        redraw();
    }

    /* F: make a face from 3 or 4 selected verts, ordered around their centroid
       on the best-fit plane so a quad doesn't come out as a bowtie. */
    void makeFace()
    {
        if (grabbing || !haveEmesh) return;
        int idx[4], n = 0, total = 0, i;
        for (i = 0; i < emesh.numVerts; i++)
            if (sel.vertSel[i]) { if (n < 4) idx[n++] = i; total++; }
        if (total < 3 || total > 4) { conLogf("make face: select 3 or 4 vertices\n"); return; }

        Vec3 c = editV3(0, 0, 0);
        for (i = 0; i < total; i++) c = editV3Add(c, emesh.verts[idx[i]].pos);
        c = editV3Scale(c, 1.0f / (float)total);

        Vec3 nrm = editV3(0, 0, 0);                 /* Newell normal */
        for (i = 0; i < total; i++) {
            Vec3 a = emesh.verts[idx[i]].pos;
            Vec3 b = emesh.verts[idx[(i + 1) % total]].pos;
            nrm.x += (a.y - b.y) * (a.z + b.z);
            nrm.y += (a.z - b.z) * (a.x + b.x);
            nrm.z += (a.x - b.x) * (a.y + b.y);
        }
        nrm = editV3Norm(nrm);
        Vec3 ref = (fabsf(nrm.y) < 0.9f) ? editV3(0, 1, 0) : editV3(1, 0, 0);
        Vec3 u = editV3Norm(editV3Cross(ref, nrm));
        Vec3 v = editV3Cross(nrm, u);
        float ang[4];
        for (i = 0; i < total; i++) {
            Vec3 d = editV3Sub(emesh.verts[idx[i]].pos, c);
            ang[i] = atan2f(editV3Dot(d, v), editV3Dot(d, u));
        }
        for (int a = 0; a < total - 1; a++)         /* bubble sort by angle */
            for (int b = 0; b < total - 1 - a; b++)
                if (ang[b] > ang[b + 1]) {
                    float t = ang[b]; ang[b] = ang[b + 1]; ang[b + 1] = t;
                    int ti = idx[b]; idx[b] = idx[b + 1]; idx[b + 1] = ti;
                }

        editHistoryPush(&hist, &emesh);
        editAddFace(&emesh, idx[0], idx[1], idx[2], total == 4 ? idx[3] : -1,
                    emesh.numMats > 0 ? 0 : -1);
        afterTopologyEdit();
        conLogf("make face: %d verts\n", total);
    }

    /* Flip the winding/normal of the selected faces (topology unchanged, so the
       selection survives). */
    void flipSelected()
    {
        if (grabbing || !haveEmesh) return;
        int n = 0, i;
        for (i = 0; i < emesh.numFaces; i++) if (sel.faceSel[i]) n++;
        if (n == 0) { conLogf("flip: select faces (mode 3) first\n"); return; }
        editHistoryPush(&hist, &emesh);
        for (i = 0; i < emesh.numFaces; i++) if (sel.faceSel[i]) editFlipFace(&emesh, i);
        editMeshBuild(&emesh, &eobj);
        updateMenuEnabled();
        redraw();
        conLogf("flip: %d face(s)\n", n);
    }

    /* Delete the selection: in vertex mode the selected verts and every face
       using them; in face/edge mode the selected faces (or faces on a selected
       edge) plus any verts orphaned by the removal. */
    void deleteSelected()
    {
        if (grabbing || !haveEmesh) return;
        int nv = emesh.numVerts, nf = emesh.numFaces, i, j;
        unsigned char *keepV = (unsigned char *)malloc(nv > 0 ? nv : 1);
        unsigned char *keepF = (unsigned char *)malloc(nf > 0 ? nf : 1);
        for (i = 0; i < nv; i++) keepV[i] = 1;
        for (i = 0; i < nf; i++) keepF[i] = 1;
        int any = 0;

        if (sel.mode == SEL_VERT) {
            for (i = 0; i < nv; i++) if (sel.vertSel[i]) { keepV[i] = 0; any = 1; }
            for (i = 0; i < nf; i++)
                for (j = 0; j < emesh.faces[i].nv; j++)
                    if (!keepV[emesh.faces[i].v[j]]) { keepF[i] = 0; break; }
        } else if (sel.mode == SEL_FACE) {
            for (i = 0; i < nf; i++) if (sel.faceSel[i]) { keepF[i] = 0; any = 1; }
        } else { /* SEL_EDGE: drop faces that contain a whole selected edge */
            for (i = 0; i < nf; i++) {
                EditFace *f = &emesh.faces[i];
                for (int e = 0; e < sel.numEdges && keepF[i]; e++) {
                    if (!sel.edgeSel[e]) continue;
                    int a = sel.edges[e].a, b = sel.edges[e].b, hasA = 0, hasB = 0;
                    for (j = 0; j < f->nv; j++) { if (f->v[j] == a) hasA = 1; if (f->v[j] == b) hasB = 1; }
                    if (hasA && hasB) { keepF[i] = 0; any = 1; }
                }
            }
        }
        if (!any) { free(keepV); free(keepF); conLogf("delete: nothing selected\n"); return; }

        if (sel.mode != SEL_VERT) {                 /* drop verts no face uses */
            for (i = 0; i < nv; i++) keepV[i] = 0;
            for (i = 0; i < nf; i++)
                if (keepF[i]) for (j = 0; j < emesh.faces[i].nv; j++)
                    keepV[emesh.faces[i].v[j]] = 1;
        }

        editHistoryPush(&hist, &emesh);
        editMeshCompact(&emesh, keepV, keepF);
        free(keepV); free(keepF);
        afterTopologyEdit();
        conLogf("delete: -> %d verts, %d faces\n", emesh.numVerts, emesh.numFaces);
    }

    void addCube()
    {
        if (grabbing || !haveEmesh) return;
        editHistoryPush(&hist, &emesh);
        Vec3 c = focusPoint();
        editAddCube(&emesh, c.x, c.y, c.z, 1.0f, 1.0f, 1.0f, emesh.numMats > 0 ? 0 : -1);
        afterTopologyEdit();
        conLogf("add cube at %.2f %.2f %.2f\n", c.x, c.y, c.z);
    }

    void addPlane()
    {
        if (grabbing || !haveEmesh) return;
        editHistoryPush(&hist, &emesh);
        Vec3 c = focusPoint();
        float h = 1.0f;
        int matId = emesh.numMats > 0 ? 0 : -1;
        editAddQuad(&emesh, editV3(c.x - h, c.y, c.z - h), editV3(c.x - h, c.y, c.z + h),
                            editV3(c.x + h, c.y, c.z + h), editV3(c.x + h, c.y, c.z - h), matId);
        afterTopologyEdit();
        conLogf("add plane at %.2f %.2f %.2f\n", c.x, c.y, c.z);
    }

    /* E: extrude the selected faces, then auto-grab the new cap so you drag it
       out to height (locked to the face's axis when it's axis-aligned). One
       undo step covers extrude+move; Esc removes the whole extrusion. */
    void extrudeSelection()
    {
        if (grabbing || !haveEmesh) return;
        int nf = emesh.numFaces, i, nsel = 0;
        for (i = 0; i < nf; i++) if (sel.faceSel[i]) nsel++;
        if (nsel == 0) { conLogf("extrude: select faces (mode 3) first\n"); return; }

        editHistoryPush(&hist, &emesh);          /* one snapshot for extrude+move */

        unsigned char *cap = (unsigned char *)malloc(nf);
        for (i = 0; i < nf; i++) cap[i] = sel.faceSel[i];

        editExtrude(&emesh, cap);                /* cap faces keep their indices */

        Vec3 avg = editV3(0, 0, 0);              /* dominant axis of the cap */
        for (i = 0; i < nf; i++) if (cap[i]) avg = editV3Add(avg, emesh.faces[i].normal);
        avg = editV3Norm(avg);

        editMeshBuild(&emesh, &eobj);
        editSelFree(&sel);
        editSelInit(&sel, &emesh);
        for (i = 0; i < nf; i++) if (cap[i]) sel.faceSel[i] = 1;   /* reselect cap */
        applyMode(SEL_FACE);
        free(cap);

        grabFromExtrude = 1;
        if (grabGather() > 0) {
            grabBeginCommon();
            if      (fabsf(avg.x) > 0.9f) grabAxis = 0;   /* lock to the face axis */
            else if (fabsf(avg.y) > 0.9f) grabAxis = 1;
            else if (fabsf(avg.z) > 0.9f) grabAxis = 2;
            else                          grabAxis = -1;
        } else {
            grabFromExtrude = 0;
        }
        refreshPanel();
        redraw();
        conLogf("extrude: %d face(s)\n", nsel);
    }

    /* ---- M5: material / tiling property panel --------------------------- */

    /* The material the panel edits: the Fl_Choice item index == material index
       (they're listed in order). */
    int panelMatId()
    {
        if (!matChoice) return -1;
        int v = matChoice->value();
        return (v >= 0 && v < emesh.numMats) ? v : -1;
    }

    void rebuildMatChoice()
    {
        if (!matChoice) return;
        int keep = matChoice->value();
        matChoice->clear();
        for (int i = 0; i < emesh.numMats; i++) {
            char nm[64];                         /* '/' and '&' are special in menus */
            const char *s = emesh.mats[i].name;
            int j = 0;
            for (; s[j] && j < 63; j++)
                nm[j] = (s[j] == '/' || s[j] == '&' || s[j] == '\\') ? '_' : s[j];
            nm[j] = 0;
            if (nm[0] == 0) { nm[0] = 'm'; nm[1] = 0; }
            matChoice->add(nm);
        }
        if (keep >= 0 && keep < emesh.numMats) matChoice->value(keep);
        else if (emesh.numMats > 0)            matChoice->value(0);
    }

    /* Show the selected faces' material (or the current choice) in the widgets. */
    void refreshPanel()
    {
        if (!matChoice) return;
        int id = -1, i;
        for (i = 0; i < emesh.numFaces; i++)
            if (sel.faceSel[i]) { id = emesh.faces[i].materialId; break; }
        if (id < 0 || id >= emesh.numMats) id = panelMatId();
        if (id < 0 && emesh.numMats > 0) id = 0;
        if (id < 0) return;
        matChoice->value(id);
        scaleInput->value(emesh.mats[id].tilingScale);
        offXInput->value(emesh.mats[id].tilingOffsetX);
        offYInput->value(emesh.mats[id].tilingOffsetY);
        diffuseInput->value(emesh.mats[id].diffusePath);
    }

    /* Live-edit the current material's tiling (shared by every face using it). */
    void onTilingChanged()
    {
        int id = panelMatId();
        if (id < 0) return;
        float s = (float)scaleInput->value();
        emesh.mats[id].tilingScale   = (s > 0.001f) ? s : 1.0f;
        emesh.mats[id].tilingOffsetX = (float)offXInput->value();
        emesh.mats[id].tilingOffsetY = (float)offYInput->value();
        editMeshBuild(&emesh, &eobj);
        redraw();
    }

    void onDiffuseChanged()
    {
        int id = panelMatId();
        if (id < 0) return;
        strncpy(emesh.mats[id].diffusePath, diffuseInput->value(), 127);
        emesh.mats[id].diffusePath[127] = 0;
        editMeshBuild(&emesh, &eobj);            /* renderer reloads by path */
        redraw();
    }

    /* Assign the chosen material to the selected faces. */
    void onMaterialChosen()
    {
        int id = matChoice->value();
        if (id < 0 || id >= emesh.numMats) return;
        for (int i = 0; i < emesh.numFaces; i++)
            if (sel.faceSel[i]) emesh.faces[i].materialId = id;
        editMeshBuild(&emesh, &eobj);
        refreshPanel();
        redraw();
    }

    void onAddMaterial()
    {
        if (emesh.numMats >= OBJ_MAX_MATERIALS) { conLogf("materials full\n"); return; }
        int id = emesh.numMats++;
        int base = panelMatId();
        if (base >= 0) emesh.mats[id] = emesh.mats[base];   /* clone current */
        else { memset(&emesh.mats[id], 0, sizeof(Material)); emesh.mats[id].tilingScale = 1.0f; }
        snprintf(emesh.mats[id].name, 64, "material_%d", id);
        rebuildMatChoice();
        matChoice->value(id);
        onMaterialChosen();                      /* assign to selection + refresh */
        conLogf("added material_%d\n", id);
    }

    int handle(int e)
    {
        switch (e) {
        case FL_PUSH:
            if (grabbing) {
                /* left = confirm the move, right = cancel it; swallow the
                   matching release so it doesn't also select. */
                if (Fl::event_button() == FL_LEFT_MOUSE)       grabConfirm();
                else if (Fl::event_button() == FL_RIGHT_MOUSE) grabCancel();
                suppressRelease = 1;
                return 1;
            }
            lastX = pushX = Fl::event_x();
            lastY = pushY = Fl::event_y();
            take_focus();
            return 1;
        case FL_RELEASE: {
            if (suppressRelease) { suppressRelease = 0; return 1; }
            /* Left click (press+release that barely moved) = select. Right
               button is camera-only, so it never selects. */
            int dx = Fl::event_x() - pushX;
            int dy = Fl::event_y() - pushY;
            if (Fl::event_button() == FL_LEFT_MOUSE && haveEmesh &&
                dx * dx + dy * dy <= 16) {
                /* EditorView is an Fl_Gl_Window (a subwindow), so event coords
                   are already local to the viewport — do NOT subtract x()/y(),
                   or the toolbar height gets double-counted and picks land ~30px
                   too high. */
                int mx = Fl::event_x();
                int my = Fl::event_y();
                int add = (Fl::event_state() & FL_SHIFT) ? 1 : 0;
                doPick((float)mx, (float)my, add);
            }
            return 1;
        }
        case FL_DRAG: {
            int dx = Fl::event_x() - lastX;
            int dy = Fl::event_y() - lastY;
            lastX = Fl::event_x();
            lastY = Fl::event_y();
            /* Right-drag orbits the camera; left-drag is reserved for selection
               (no rotation), so a left click-select never fights the look. */
            if (Fl::event_state() & FL_BUTTON3) {
                yaw   += dx * 0.005f;
                pitch -= dy * 0.005f;
                if (pitch >  1.55f) pitch =  1.55f;
                if (pitch < -1.55f) pitch = -1.55f;
                redraw();
            }
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
        case FL_ENTER:
            return 1;               /* claim belowmouse so FL_MOVE arrives */
        case FL_MOVE:
            if (grabbing) grabUpdate(Fl::event_x(), Fl::event_y());
            return 1;
        case FL_FOCUS:
        case FL_UNFOCUS:
            return 1;               /* keep receiving keyboard focus */
        case FL_KEYDOWN: {
            int k  = Fl::event_key();
            int st = Fl::event_state();
            if (grabbing) {
                /* axis locks toggle (press again to release); Enter confirms,
                   Esc cancels. Everything else is swallowed mid-grab. */
                if (k == 'x' || k == 'X') { grabAxis = (grabAxis == 0) ? -1 : 0; grabApply(); return 1; }
                if (k == 'y' || k == 'Y') { grabAxis = (grabAxis == 1) ? -1 : 1; grabApply(); return 1; }
                if (k == 'z' || k == 'Z') { grabAxis = (grabAxis == 2) ? -1 : 2; grabApply(); return 1; }
                if (k == FL_Enter || k == FL_KP_Enter) { grabConfirm(); return 1; }
                if (k == FL_Escape) { grabCancel(); return 1; }
                return 1;
            }
            /* 1/2/3 select mode; G grab; Ctrl+Z / Ctrl+Y undo/redo. WASD/QE
               movement is polled by the frame timer, so it's untouched here. */
            if (k == '1') { applyMode(SEL_VERT); return 1; }
            if (k == '2') { applyMode(SEL_EDGE); return 1; }
            if (k == '3') { applyMode(SEL_FACE); return 1; }
            if (k == 'g' || k == 'G') { grabStart(); return 1; }
            if (k == 'f' || k == 'F') { makeFace(); return 1; }
            if (k == 'e' || k == 'E') { extrudeSelection(); return 1; }
            if (k == 'x' || k == 'X' || k == FL_Delete) { deleteSelected(); return 1; }
            if ((st & FL_CTRL) && (k == 'z' || k == 'Z')) { doUndo(); return 1; }
            if ((st & FL_CTRL) && (k == 'y' || k == 'Y')) { doRedo(); return 1; }
            return 1;
        }
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

/* Toolbar select-mode buttons: route through the same applyMode() the 1/2/3
   keys use, so keyboard and toolbar stay in sync. */
static void modeButtonCb(Fl_Widget *w, void *v)
{
    EditorView *view = (EditorView *)v;
    if (w == view->bVert)      view->applyMode(SEL_VERT);
    else if (w == view->bEdge) view->applyMode(SEL_EDGE);
    else                       view->applyMode(SEL_FACE);
}

/* Menu callbacks. Undo/Redo also have Ctrl+Z/Y accelerators; the viewport
   handles those keys when focused, so exactly one undo fires either way. */
static int confirmExit()
{
    /* b0 "Cancel" is the default (Enter/Esc) — safe default for a destructive
       action; returns 1 only when "Exit" is explicitly chosen. */
    return fl_choice("Exit the SOOB editor?", "Cancel", "Exit", 0) == 1;
}
static void menuExitCb(Fl_Widget *, void *v) { if (confirmExit()) ((Fl_Window *)v)->hide(); }
static void winCloseCb(Fl_Widget *w, void *) { if (confirmExit()) w->hide(); }
static void menuUndoCb(Fl_Widget *, void *v) { ((EditorView *)v)->doUndo(); }
static void menuRedoCb(Fl_Widget *, void *v) { ((EditorView *)v)->doRedo(); }
static void menuExtrudeCb (Fl_Widget *, void *v) { ((EditorView *)v)->extrudeSelection(); }
static void menuAddCubeCb (Fl_Widget *, void *v) { ((EditorView *)v)->addCube(); }
static void menuAddPlaneCb(Fl_Widget *, void *v) { ((EditorView *)v)->addPlane(); }
static void menuMakeFaceCb(Fl_Widget *, void *v) { ((EditorView *)v)->makeFace(); }
static void menuFlipCb    (Fl_Widget *, void *v) { ((EditorView *)v)->flipSelected(); }
static void menuDeleteCb  (Fl_Widget *, void *v) { ((EditorView *)v)->deleteSelected(); }

/* Property-panel callbacks. */
static void matChoiceCb(Fl_Widget *, void *v) { ((EditorView *)v)->onMaterialChosen(); }
static void addMatCb   (Fl_Widget *, void *v) { ((EditorView *)v)->onAddMaterial(); }
static void diffuseCb  (Fl_Widget *, void *v) { ((EditorView *)v)->onDiffuseChanged(); }
static void tilingCb   (Fl_Widget *, void *v) { ((EditorView *)v)->onTilingChanged(); }

int main(int argc, char **argv)
{
    Fl::gl_visual(FL_RGB | FL_DEPTH | FL_DOUBLE);

    const int W = 1024, H = 768;
    const int MB = 25;                 /* menu-bar height */
    const int TB = 30;                 /* toolbar height */
    const int PW = 220;                /* right property-panel width */
    const int TOP = MB + TB;           /* content top (below menu + toolbar) */

    Fl_Window *win = new Fl_Window(W, H, "SOOB Level Editor");
    win->begin();

    /* Menu bar. Child widget coords are absolute (window space), so the toolbar
       and everything below it are offset by the menu/toolbar heights. */
    Fl_Menu_Bar *menu = new Fl_Menu_Bar(0, 0, W, MB);

    /* Toolbar: select-mode radio buttons, icons from editor/icons/*.xpm.
       FL_RADIO_BUTTON gives one-lit-at-a-time behaviour for free. */
    Fl_Group *toolbar = new Fl_Group(0, MB, W, TB);
    toolbar->box(FL_UP_BOX);
    Fl_Button *bVert = new Fl_Button(4,  MB + 3, 34, TB - 6);
    Fl_Button *bEdge = new Fl_Button(40, MB + 3, 34, TB - 6);
    Fl_Button *bFace = new Fl_Button(76, MB + 3, 34, TB - 6);
    bVert->image(new Fl_Pixmap(mode_vert_xpm)); bVert->type(FL_RADIO_BUTTON);
    bEdge->image(new Fl_Pixmap(mode_edge_xpm)); bEdge->type(FL_RADIO_BUTTON);
    bFace->image(new Fl_Pixmap(mode_face_xpm)); bFace->type(FL_RADIO_BUTTON);
    bVert->tooltip("Vertex select (1)");
    bEdge->tooltip("Edge select (2)");
    bFace->tooltip("Face select (3)");
    toolbar->end();
    toolbar->resizable(NULL);          /* buttons stay put when the window resizes */

    /* Right-side property panel — material + tiling of the selected faces.
       Fixed width, anchored right. */
    Fl_Group *panel = new Fl_Group(W - PW, TOP, PW, H - TOP);
    panel->box(FL_UP_BOX);
    Fl_Box *ptitle = new Fl_Box(W - PW, TOP, PW, 22, "Properties");
    ptitle->labelfont(FL_HELVETICA_BOLD);
    ptitle->align(FL_ALIGN_INSIDE | FL_ALIGN_CENTER);

    int px = W - PW + 74, pw = PW - 82, yy = TOP + 34;
    Fl_Choice      *mc = new Fl_Choice(px, yy, pw, 22, "Material:");     yy += 28;
    Fl_Button      *ab = new Fl_Button(W - PW + 8, yy, PW - 16, 22, "Add Material"); yy += 30;
    Fl_Input       *di = new Fl_Input(px, yy, pw, 22, "Diffuse:");       yy += 30;
    Fl_Value_Input *si = new Fl_Value_Input(px, yy, pw, 22, "Tile Scale:"); yy += 26;
    Fl_Value_Input *ox = new Fl_Value_Input(px, yy, pw, 22, "Offset X:");   yy += 26;
    Fl_Value_Input *oy = new Fl_Value_Input(px, yy, pw, 22, "Offset Y:");   yy += 26;
    si->range(0.01, 64.0); si->step(0.05); si->value(1.0);
    ox->range(-64.0, 64.0); ox->step(0.05); ox->value(0.0);
    oy->range(-64.0, 64.0); oy->step(0.05); oy->value(0.0);
    di->when(FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE);
    panel->end();
    panel->resizable(NULL);

    /* GL viewport fills the area left of the panel, below menu + toolbar. */
    EditorView *view = new EditorView(0, TOP, W - PW, H - TOP);
    if (argc > 1) view->objPath = argv[1];
    if (argc > 2) view->entPath = argv[2];

    win->end();
    win->resizable(view);              /* only the viewport grows/shrinks */

    /* Wire the toolbar buttons to the view, then light the initial mode. */
    view->bVert = bVert; view->bEdge = bEdge; view->bFace = bFace;
    bVert->callback(modeButtonCb, view);
    bEdge->callback(modeButtonCb, view);
    bFace->callback(modeButtonCb, view);
    view->applyMode(SEL_VERT);

    /* Wire the property panel (widgets built above, in the panel group). */
    view->matChoice = mc; view->diffuseInput = di;
    view->scaleInput = si; view->offXInput = ox; view->offYInput = oy;
    mc->callback(matChoiceCb, view);
    ab->callback(addMatCb, view);
    di->callback(diffuseCb, view);
    si->callback(tilingCb, view);
    ox->callback(tilingCb, view);
    oy->callback(tilingCb, view);

    /* Menu items (added after `view` exists for the Edit callbacks). Stash the
       Undo/Redo item indices so the view can grey them out when empty. */
    menu->add("File/Exit", 0, menuExitCb, win);
    view->menuBar = menu;
    view->undoIdx = menu->add("Edit/Undo", FL_COMMAND + 'z', menuUndoCb, view);
    view->redoIdx = menu->add("Edit/Redo", FL_COMMAND + 'y', menuRedoCb, view);
    view->updateMenuEnabled();             /* both start greyed (empty stacks) */

    menu->add("Add/Cube",          0,         menuAddCubeCb,  view);
    menu->add("Add/Plane",         0,         menuAddPlaneCb, view);
    menu->add("Mesh/Extrude",      'e',       menuExtrudeCb,  view);
    menu->add("Mesh/Make Face",    'f',       menuMakeFaceCb, view);
    menu->add("Mesh/Flip Normals", 0,         menuFlipCb,     view);
    menu->add("Mesh/Delete",       FL_Delete, menuDeleteCb,   view);

    win->callback(winCloseCb);             /* confirm on the window close button too */

    conLogf("SOOB Level Editor — right-drag look, WASD move, PgUp/PgDn up/down, wheel dolly\n");
    conLogf("  left-click = select, Shift+left-click = add/toggle, 1/2/3 = vertex/edge/face mode\n");
    conLogf("  G = grab (X/Y/Z lock, click/Enter confirm, Esc cancel), Ctrl+Z / Ctrl+Y = undo/redo\n");
    conLogf("  E = extrude, F = make face (3-4 verts), X/Del = delete; Add & Mesh menus\n");
    conLogf("Loading %s / %s (run from repo root)\n", view->objPath, view->entPath);

    win->show(argc, argv);
    view->take_focus();
    Fl::add_timeout(1.0 / 60.0, frameTimer, view);

    return Fl::run();
}
