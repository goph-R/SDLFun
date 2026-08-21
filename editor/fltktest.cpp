/* Minimal FLTK probe for the Win98 box.
 *
 * The editor loads fully but never paints its Fl_Gl_Window, so draw() -- and
 * with it bootstrap() -- never runs. That could mean FLTK cannot show a window
 * here at all, or that only the GL child is affected. This separates the two.
 *
 * Stage 1 shows a plain Fl_Window with a label. Stage 2 adds an Fl_Gl_Window
 * child and reports whether it ever receives a draw. Both stages report to
 * stdout, flushed, because Fl::error goes to stderr and COMMAND.COM has no
 * 2>&1. Build with t98.bat; close the window to exit.
 */
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/gl.h>
#include <stdio.h>
#include <stdarg.h>

static void msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static void flMsg(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    msg("FLTK: %s\n", buf);
}

class GlProbe : public Fl_Gl_Window {
public:
    int drew;
    GlProbe(int X, int Y, int W, int H) : Fl_Gl_Window(X, Y, W, H), drew(0) {}
    void draw()
    {
        if (!drew) {
            drew = 1;
            const char *r = (const char *)glGetString(GL_RENDERER);
            const char *v = (const char *)glGetString(GL_VERSION);
            msg("probe: FIRST GL DRAW -- renderer=%s version=%s\n",
                r ? r : "(none)", v ? v : "(none)");
        }
        glClearColor(0.0f, 0.4f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
};

int main(int argc, char **argv)
{
    Fl::error = flMsg;
    Fl::warning = flMsg;

    msg("probe: stage 1 - plain Fl_Window\n");
    Fl_Window *win = new Fl_Window(480, 360, "SOOB FLTK probe");
    win->begin();
    Fl_Box *box = new Fl_Box(10, 10, 460, 40, "If you can read this, plain FLTK works.");
    box->box(FL_UP_BOX);

    msg("probe: stage 2 - Fl_Gl_Window child, can_do(RGB|DEPTH|DOUBLE)=%d\n",
        Fl_Gl_Window::can_do(FL_RGB | FL_DEPTH | FL_DOUBLE));
    GlProbe *gl = new GlProbe(10, 60, 460, 290);
    gl->mode(FL_RGB | FL_DEPTH | FL_DOUBLE);
    win->end();

    win->show(argc, argv);
    msg("probe: after show() - win->shown()=%d gl->shown()=%d\n",
        win->shown(), gl->shown());

    for (int i = 0; i < 60 && !gl->drew; i++) Fl::wait(0.05);

    msg("probe: after 3s pump - gl drew=%d context=%p\n",
        gl->drew, (void *)gl->context());
    if (!gl->drew)
        msg("probe: GL child never painted. If the window itself IS visible,\n"
            "probe: the problem is the GL child specifically, not FLTK.\n");

    msg("probe: entering Fl::run() - close the window to exit\n");
    return Fl::run();
}
