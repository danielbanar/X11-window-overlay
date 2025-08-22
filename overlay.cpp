#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

#define ALIGN_CENTER 1
#define ALIGN_LEFT 2
#define ALIGN_RIGHT 3
#define BASIC_EVENT_MASK (StructureNotifyMask | ExposureMask | PropertyChangeMask | EnterWindowMask | LeaveWindowMask | KeyPressMask | KeyReleaseMask | KeymapStateMask)
#define NOT_PROPAGATE_MASK (KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ButtonMotionMask)

Display *g_display;
int g_screen;
Window g_win;
Colormap g_colormap;

cairo_surface_t *cairo_surface = nullptr;
cairo_surface_t *offscreen_surface = nullptr;
cairo_t *cr = nullptr;

int WIDTH = 640;
int HEIGHT = 480;
int POSX = 0;
int POSY = 0;

void allow_input_passthrough(Window w)
{
    XserverRegion region = XFixesCreateRegion(g_display, NULL, 0);
    XFixesSetWindowShapeRegion(g_display, w, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(g_display, region);
}

bool findWindowByClass(Window root, const std::string &target_class, Window &outWin)
{
    Window root_return, parent_return;
    Window *children;
    unsigned int nchildren;
    if (XQueryTree(g_display, root, &root_return, &parent_return, &children, &nchildren))
    {
        for (unsigned int i = 0; i < nchildren; i++)
        {
            XClassHint classHint;
            if (XGetClassHint(g_display, children[i], &classHint))
            {
                bool match = false;
                if (classHint.res_class)
                {
                    std::string cls(classHint.res_class);
                    if (cls == target_class)
                        match = true;
                }
                if (classHint.res_name)
                    XFree(classHint.res_name);
                if (classHint.res_class)
                    XFree(classHint.res_class);

                if (match)
                {
                    outWin = children[i];
                    if (children)
                        XFree(children);
                    return true;
                }
            }
            if (findWindowByClass(children[i], target_class, outWin))
            {
                if (children)
                    XFree(children);
                return true;
            }
        }
        if (children)
            XFree(children);
    }
    return false;
}

void getWindowGeometry(Window win)
{
    XWindowAttributes attr;
    XGetWindowAttributes(g_display, win, &attr);

    Window child;
    int x, y;
    XTranslateCoordinates(g_display, win, DefaultRootWindow(g_display), 0, 0, &x, &y, &child);

    POSX = x;
    POSY = y;
    WIDTH = attr.width;
    HEIGHT = attr.height;
}

void createOverlayWindow()
{
    XVisualInfo vinfo;
    XMatchVisualInfo(g_display, DefaultScreen(g_display), 32, TrueColor, &vinfo);

    g_colormap = XCreateColormap(g_display, DefaultRootWindow(g_display), vinfo.visual, AllocNone);
    XSetWindowAttributes attr{};
    attr.background_pixmap = None;
    attr.border_pixel = 0;
    attr.override_redirect = 1;
    attr.colormap = g_colormap;
    attr.event_mask = BASIC_EVENT_MASK;
    attr.do_not_propagate_mask = NOT_PROPAGATE_MASK;

    unsigned long mask = CWColormap | CWBorderPixel | CWEventMask |
                         CWDontPropagate | CWOverrideRedirect;

    g_win = XCreateWindow(g_display, DefaultRootWindow(g_display), POSX, POSY, WIDTH, HEIGHT, 0,
                          vinfo.depth, InputOutput, vinfo.visual, mask, &attr);

    XShapeCombineMask(g_display, g_win, ShapeInput, 0, 0, None, ShapeSet);
    allow_input_passthrough(g_win);
    XMapWindow(g_display, g_win);

    cairo_surface = cairo_xlib_surface_create(g_display, g_win, vinfo.visual, WIDTH, HEIGHT);
    cr = cairo_create(cairo_surface);

    // Create offscreen buffer for double buffering
    offscreen_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, WIDTH, HEIGHT);
}

void drawText(cairo_t *ctx, const std::string &text, int x, int y, double r, double g, double b, int align)
{
    PangoLayout *layout = pango_cairo_create_layout(ctx);

    // You can change to "Ubuntu Mono 24" or load your custom TTF using fontconfig if installed
    PangoFontDescription *desc = pango_font_description_from_string("Consolas 24");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    pango_layout_set_text(layout, text.c_str(), -1);

    int width, height;
    pango_layout_get_size(layout, &width, &height);
    double w = width / PANGO_SCALE;
    double h = height / PANGO_SCALE;

    double tx = x;
    if (align == ALIGN_CENTER)
        tx = x - w / 2.0;
    else if (align == ALIGN_RIGHT)
        tx = x - w;

    cairo_set_source_rgba(ctx, r, g, b, 1.0);
    cairo_move_to(ctx, tx, y - h / 2.0);
    pango_cairo_show_layout(ctx, layout);

    g_object_unref(layout);
}

void ensureOffscreenBuffer()
{
    static int lastW = 0, lastH = 0;
    if (lastW != WIDTH || lastH != HEIGHT || !offscreen_surface)
    {
        if (offscreen_surface)
            cairo_surface_destroy(offscreen_surface);

        offscreen_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, WIDTH, HEIGHT);
        lastW = WIDTH;
        lastH = HEIGHT;
    }
}

void renderFrame(const std::string &text)
{
    ensureOffscreenBuffer();

    cairo_t *off = cairo_create(offscreen_surface);

    // Clear completely with transparency
    cairo_set_operator(off, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(off, 0, 0, 0, 0);
    cairo_paint(off);

    drawText(off, text, WIDTH / 2, HEIGHT / 2, 1.0, 1.0, 1.0, ALIGN_CENTER);

    cairo_destroy(off);

    // Blit offscreen to X11 surface (replace, not blend)
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, offscreen_surface, 0, 0);
    cairo_paint(cr);
}

int main()
{
    g_display = XOpenDisplay(0);
    if (!g_display)
    {
        std::cerr << "Failed to open X display" << std::endl;
        return 1;
    }

    Window target;
    if (!findWindowByClass(DefaultRootWindow(g_display), "GStreamer", target))
    {
        std::cerr << "Could not find window with class 'GStreamer'\n";
        return 1;
    }

    getWindowGeometry(target);
    createOverlayWindow();

    auto start_time = std::chrono::steady_clock::now();

    while (true)
    {
        getWindowGeometry(target);
        XMoveResizeWindow(g_display, g_win, POSX, POSY, WIDTH, HEIGHT);
        cairo_xlib_surface_set_size(cairo_surface, WIDTH, HEIGHT);

        cairo_xlib_surface_set_size(cairo_surface, WIDTH, HEIGHT);

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

        std::string text = std::to_string(elapsed) + " ms 🔋🧭🛰️⏱🏠";

        renderFrame(text);

        cairo_surface_flush(cairo_surface);
        XFlush(g_display);
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 20 FPS to prevent flicker
    }

    cairo_destroy(cr);
    cairo_surface_destroy(cairo_surface);
    cairo_surface_destroy(offscreen_surface);
    XCloseDisplay(g_display);
    return 0;
}
