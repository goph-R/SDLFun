#include <SDL/SDL.h>
#include <GL/gl.h>
#include <cstdlib>

int main(int argc, char *argv[])
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Surface *screen = SDL_SetVideoMode(640, 480, 32, SDL_OPENGL);
    if (!screen) {
        SDL_Quit();
        return 1;
    }

    SDL_WM_SetCaption("SDL Fun - OpenGL", NULL);

    /* Set up a simple 3D perspective */
    glViewport(0, 0, 640, 480);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    /* Simple perspective: fov ~60 degrees */
    float aspect = 640.0f / 480.0f;
    float fov = 1.0f; /* ~60 deg half-angle tangent */
    glFrustum(-fov * aspect * 0.1, fov * aspect * 0.1,
              -fov * 0.1, fov * 0.1, 0.1, 100.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_DEPTH_TEST);

    float angle = 0.0f;

    /* Main loop */
    SDL_Event event;
    int running = 1;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = 0;
            }
        }

        /* Clear screen */
        glClearColor(0.0f, 0.0f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Draw a spinning colored cube */
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -4.0f);
        glRotatef(angle, 1.0f, 1.0f, 0.0f);

        glBegin(GL_QUADS);
            /* Front - red */
            glColor3f(1.0f, 0.0f, 0.0f);
            glVertex3f(-1, -1,  1);
            glVertex3f( 1, -1,  1);
            glVertex3f( 1,  1,  1);
            glVertex3f(-1,  1,  1);
            /* Back - green */
            glColor3f(0.0f, 1.0f, 0.0f);
            glVertex3f(-1, -1, -1);
            glVertex3f(-1,  1, -1);
            glVertex3f( 1,  1, -1);
            glVertex3f( 1, -1, -1);
            /* Top - blue */
            glColor3f(0.0f, 0.0f, 1.0f);
            glVertex3f(-1,  1, -1);
            glVertex3f(-1,  1,  1);
            glVertex3f( 1,  1,  1);
            glVertex3f( 1,  1, -1);
            /* Bottom - yellow */
            glColor3f(1.0f, 1.0f, 0.0f);
            glVertex3f(-1, -1, -1);
            glVertex3f( 1, -1, -1);
            glVertex3f( 1, -1,  1);
            glVertex3f(-1, -1,  1);
            /* Right - magenta */
            glColor3f(1.0f, 0.0f, 1.0f);
            glVertex3f( 1, -1, -1);
            glVertex3f( 1,  1, -1);
            glVertex3f( 1,  1,  1);
            glVertex3f( 1, -1,  1);
            /* Left - cyan */
            glColor3f(0.0f, 1.0f, 1.0f);
            glVertex3f(-1, -1, -1);
            glVertex3f(-1, -1,  1);
            glVertex3f(-1,  1,  1);
            glVertex3f(-1,  1, -1);
        glEnd();

        SDL_GL_SwapBuffers();

        angle += 0.5f;
        if (angle >= 360.0f) angle -= 360.0f;

        SDL_Delay(16);
    }

    SDL_Quit();
    return 0;
}
