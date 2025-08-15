#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>

#define ALIGN_CENTER 1
#define ALIGN_LEFT   2
#define ALIGN_RIGHT  3
#define BASIC_EVENT_MASK (StructureNotifyMask | ExposureMask | PropertyChangeMask | EnterWindowMask | LeaveWindowMask | KeyPressMask | KeyReleaseMask | KeymapStateMask)
#define NOT_PROPAGATE_MASK (KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ButtonMotionMask)

Display *g_display;
int g_screen;
Window g_win;
Colormap g_colormap;
GC gc;
XFontStruct *font;
XColor ltblue, blacka, transparent, white;

const char *font_name = "9x15bold";
const int font_width = 9;
const int font_height = 15;
int WIDTH = 640;
int HEIGHT = 480;
int POSX = 0;
int POSY = 0;

XColor createXColorFromRGB(short r, short g, short b)
{
    XColor color;
    color.red = (r * 0xFFFF) / 0xFF;
    color.green = (g * 0xFFFF) / 0xFF;
    color.blue = (b * 0xFFFF) / 0xFF;
    color.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(g_display, DefaultColormap(g_display, g_screen), &color))
    {
        std::cerr << "Cannot create color" << std::endl;
        exit(1);
    }
    return color;
}

XColor createXColorFromRGBA(short r, short g, short b, short a)
{
    XColor color = createXColorFromRGB(r, g, b);
    *(&color.pixel) = ((*(&color.pixel)) & 0x00ffffff) | (a << 24);
    return color;
}

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
                    {
                        match = true;
                    }
                }
                // Free class hint resources
                if (classHint.res_name) XFree(classHint.res_name);
                if (classHint.res_class) XFree(classHint.res_class);

                if (match)
                {
                    outWin = children[i];
                    if (children)
                        XFree(children);
                    return true;
                }
            }

            // Recursive search
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

    //std::cout << "Found target window at (" << POSX << "," << POSY << "), size " << WIDTH << "x" << HEIGHT << "\n";
}

void createOverlayWindow()
{
    XColor bgcolor = createXColorFromRGBA(0, 0, 0, 0);
    XVisualInfo vinfo;
    XMatchVisualInfo(g_display, DefaultScreen(g_display), 32, TrueColor, &vinfo);

    g_colormap = XCreateColormap(g_display, DefaultRootWindow(g_display), vinfo.visual, AllocNone);
    XSetWindowAttributes attr{};
    attr.background_pixmap = None;
    attr.background_pixel = bgcolor.pixel;
    attr.border_pixel = 0;
    attr.win_gravity = NorthWestGravity;
    attr.bit_gravity = ForgetGravity;
    attr.save_under = 1;
    attr.event_mask = BASIC_EVENT_MASK;
    attr.do_not_propagate_mask = NOT_PROPAGATE_MASK;
    attr.override_redirect = 1;
    attr.colormap = g_colormap;

    unsigned long mask = CWColormap | CWBorderPixel | CWBackPixel | CWEventMask |
                         CWWinGravity | CWBitGravity | CWSaveUnder | CWDontPropagate | CWOverrideRedirect;

    g_win = XCreateWindow(g_display, DefaultRootWindow(g_display), POSX, POSY, WIDTH, HEIGHT, 0,
                          vinfo.depth, InputOutput, vinfo.visual, mask, &attr);

    XShapeCombineMask(g_display, g_win, ShapeInput, 0, 0, None, ShapeSet);
    allow_input_passthrough(g_win);
    XMapWindow(g_display, g_win);
}

void initOverlay()
{
    g_screen = DefaultScreen(g_display);
    createOverlayWindow();

    gc = XCreateGC(g_display, g_win, 0, 0);
    font = XLoadQueryFont(g_display, font_name);
    if (!font)
    {
        std::cerr << "Unable to load font " << font_name << std::endl;
        font = XLoadQueryFont(g_display, "fixed");
    }

    ltblue = createXColorFromRGBA(0, 255, 255, 255);
    blacka = createXColorFromRGBA(0, 0, 0, 150);
    white = createXColorFromRGBA(255, 255, 255, 255);
    transparent = createXColorFromRGBA(255, 255, 255, 0);
}

void drawString(const char *text, int x, int y, XColor fg, int align)
{
    int tlen = strlen(text);
    int text_width = tlen * font_width;
    int text_x = x;

    // Adjust positions based on alignment
    switch(align) {
        case ALIGN_CENTER:
            text_x = x - text_width / 2;
            break;
        case ALIGN_RIGHT:
            text_x = x - text_width;
            break;
        case ALIGN_LEFT:
        default:
            // Already set to left alignment by default
            break;
    }

    XSetFont(g_display, gc, font->fid);
    XSetForeground(g_display, gc, fg.pixel);
    XDrawString(g_display, g_win, gc, text_x, y + font_height, text, tlen);
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
    initOverlay();

    auto start_time = std::chrono::steady_clock::now();

    while (true)
    {
        // Recalculate GStreamer window position & size
        getWindowGeometry(target);

        // Move & resize overlay if needed
        XMoveResizeWindow(g_display, g_win, POSX, POSY, WIDTH, HEIGHT);

        // Clear the window before redrawing
        XClearWindow(g_display, g_win);

        // Get elapsed time
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        std::string text = std::to_string(elapsed) + " ms";
        const char* timer_text = text.c_str();

        // Draw timer in multiple positions without backgrounds
        int padding = 10;
        int vertical_padding = 20;
        
        // Top-left
        drawString(timer_text, padding, vertical_padding, ltblue, ALIGN_LEFT);
        
        // Top-middle
        drawString(timer_text, WIDTH / 2, vertical_padding, white, ALIGN_CENTER);
        
        // Top-right
        drawString(timer_text, WIDTH - padding, vertical_padding, ltblue, ALIGN_RIGHT);
        
        // Bottom-middle
        drawString(timer_text, WIDTH / 2, HEIGHT - vertical_padding, white, ALIGN_CENTER);
        
        // Center
        drawString(timer_text, WIDTH / 2, HEIGHT / 2, ltblue, ALIGN_CENTER);

        XFlush(g_display);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}