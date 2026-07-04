#ifndef RENDER_WORLD_H
#define RENDER_WORLD_H

/* ---- World render module (header-only, static) ----
 *
 * One layer above render_level.h: draws a whole game world (lit level +
 * entities) from a caller-supplied camera. Shared verbatim between the game
 * (main.cpp) and the SOOB level editor so both see identical geometry,
 * lightmaps, and entity placement.
 *
 * renderWorld() is DRAW-ONLY. It does not clear the framebuffer, set the
 * projection matrix, or step simulation — those are the caller's job:
 *   - the game clears with its sky colour, then overlays gun/crosshair/HUD;
 *   - the editor clears per-viewport, then overlays grid/gizmos/selection.
 * Simulation (entUpdate, door/platform updates, the flashlight's dynamic
 * lightmap) also stays in the caller — renderWorld never mutates game state.
 * Projection is assumed already current (set at init / on resize).
 *
 * Include order (single-TU header convention): after obj_loader.h (Vec3),
 * render_level.h (renderLevelSectored, glLookAt), entity.h (entRender), and
 * game.h (Game). Include this last of that group.
 */

#include <GL/gl.h>

/* View for a single frame. rollDeg is a view-space bank applied before the
   look-at (HL1 strafe roll in the game); the editor passes 0. */
typedef struct {
    Vec3  eye;
    Vec3  at;
    float rollDeg;
} Camera;

static void renderWorld(Game *game, Camera cam)
{
    glEnable(GL_LIGHTING);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* Optional view-space roll, BEFORE glLookAt so the matrix order is
       M = R * L (roll in view space, look-at maps world→view first). */
    if (cam.rollDeg != 0.0f)
        glRotatef(cam.rollDeg, 0.0f, 0.0f, 1.0f);

    glLookAt(cam.eye, cam.at);

    /* Fallback directional light anchored at world (0,10,0). Submitted AFTER
       the view matrix is on the stack — glLightfv transforms the position by
       the current modelview. (Ignored by lightmapped sectors, which disable
       GL_LIGHTING; used for entities and un-lightmapped geometry.) */
    float lp[] = { 0.0f, 10.0f, 0.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lp);

    renderLevelSectored(&game->level, &game->texCache);

    /* Entities. entUpdate / door / platform stepping is simulation and lives
       in the caller — renderWorld only draws what's already been stepped. */
    glEnable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 1.0f);
    entRender(game->entities);
}

#endif /* RENDER_WORLD_H */
