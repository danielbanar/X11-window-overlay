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

#define ALIGN_LEFT 0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT 2
#define BASIC_EVENT_MASK (StructureNotifyMask|ExposureMask|PropertyChangeMask|EnterWindowMask|LeaveWindowMask|KeyPressMask|KeyReleaseMask|KeymapStateMask)
#define NOT_PROPAGATE_MASK (KeyPressMask|KeyReleaseMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|ButtonMotionMask)
#define SHAPE_MASK ShapeNotifyMask

// Global X11 variables
Display *g_display;
int g_screen;
Window g_win;
Colormap g_colormap;
GC gc;
XFontStruct *font;

// Colors
XColor red, black, blacka, ltblue, transparent, white;

// Font
const char *font_name = "9x15bold";
const int font_width = 9;
const int font_height = 15;

// Window size
int WIDTH  = 1920;
int HEIGHT = 1080;

XColor createXColorFromRGB(short r, short g, short b) {
    XColor color;
    color.red   = (r * 0xFFFF) / 0xFF;
    color.green = (g * 0xFFFF) / 0xFF;
    color.blue  = (b * 0xFFFF) / 0xFF;
    color.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(g_display, DefaultColormap(g_display, g_screen), &color)) {
        std::cerr << "Cannot create color" << std::endl;
        exit(1);
    }
    return color;
}

XColor createXColorFromRGBA(short r, short g, short b, short a) {
    XColor color = createXColorFromRGB(r, g, b);
    *(&color.pixel) = ((*(&color.pixel)) & 0x00ffffff) | (a << 24);
    return color;
}

void allow_input_passthrough(Window w) {
    XserverRegion region = XFixesCreateRegion(g_display, NULL, 0);
    XFixesSetWindowShapeRegion(g_display, w, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(g_display, region);
}

void openDisplay() {
    g_display = XOpenDisplay(0);
    if (!g_display) {
        std::cerr << "Failed to open X display" << std::endl;
        exit(1);
    }
    g_screen = DefaultScreen(g_display);
    int shape_event_base, shape_error_base;
    if (!XShapeQueryExtension(g_display, &shape_event_base, &shape_error_base)) {
        std::cerr << "NO shape extension!" << std::endl;
        exit(1);
    }
}

void createShapedWindow() {
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

    unsigned long mask = CWColormap | CWBorderPixel | CWBackPixel | CWEventMask | CWWinGravity |
                         CWBitGravity | CWSaveUnder | CWDontPropagate | CWOverrideRedirect;

    g_win = XCreateWindow(g_display, DefaultRootWindow(g_display), 0, 0, WIDTH, HEIGHT, 0,
                          vinfo.depth, InputOutput, vinfo.visual, mask, &attr);

    XShapeCombineMask(g_display, g_win, ShapeInput, 0, 0, None, ShapeSet);
    allow_input_passthrough(g_win);
    XMapWindow(g_display, g_win);
}

void initOverlay() {
    openDisplay();
    createShapedWindow();

    gc = XCreateGC(g_display, g_win, 0, 0);
    font = XLoadQueryFont(g_display, font_name);
    if (!font) {
        std::cerr << "Unable to load font " << font_name << std::endl;
        font = XLoadQueryFont(g_display, "fixed");
    }

    red   = createXColorFromRGBA(255, 0, 0, 255);
    ltblue = createXColorFromRGBA(0, 255, 255, 255);
    black = createXColorFromRGBA(0, 0, 0, 255);
    blacka = createXColorFromRGBA(0, 0, 0, 150);
    white = createXColorFromRGBA(255, 255, 255, 255);
    transparent = createXColorFromRGBA(255, 255, 255, 0);
}

void drawString(const char *text, int x, int y, XColor fg, XColor bg, int align) {
    int tlen = strlen(text);
    XSetFont(g_display, gc, font->fid);
    if (bg.pixel != transparent.pixel) {
        XSetForeground(g_display, gc, bg.pixel);
        XFillRectangle(g_display, g_win, gc,
                       x - (align == ALIGN_CENTER ? tlen*font_width/2 : (align == ALIGN_RIGHT ? tlen*font_width : 0)) - 4,
                       y, tlen*font_width + 8, font_height + 4);
    }
    XSetForeground(g_display, gc, fg.pixel);
    XDrawString(g_display, g_win, gc,
                x - (align == ALIGN_CENTER ? tlen*font_width/2 : (align == ALIGN_RIGHT ? tlen*font_width : 0)),
                y + font_height, text, tlen);
}

int main() {
    initOverlay();

    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

        std::string text = std::to_string(elapsed) + " ms";
        drawString(text.c_str(), WIDTH/2, HEIGHT/2, ltblue, blacka, ALIGN_CENTER);

        XFlush(g_display);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
