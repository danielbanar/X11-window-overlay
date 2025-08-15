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
#include <X11/Xft/Xft.h> // Include Xft headers

#define ALIGN_CENTER 1
#define ALIGN_LEFT   2
#define ALIGN_RIGHT  3
#define BASIC_EVENT_MASK (StructureNotifyMask | ExposureMask | PropertyChangeMask | EnterWindowMask | LeaveWindowMask | KeyPressMask | KeyReleaseMask | KeymapStateMask)
#define NOT_PROPAGATE_MASK (KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ButtonMotionMask)

Display *g_display;
int g_screen;
Window g_win;
Colormap g_colormap;
XVisualInfo g_vinfo; // Store visual info globally
GC gc;
XftFont *xft_font = nullptr; // Xft font structure
XftDraw *xft_draw = nullptr; // Xft drawing context

// Xft color variables
XftColor ltblue_xft;
XftColor blacka_xft;
XftColor white_xft;
XftColor outline_xft;

const char *font_path = "/home/dan/repos/X11-window-overlay/fonts/consolas.ttf"; // Update this path
const int font_size = 15; // Adjust as needed
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

XftColor createXftColorFromRGBA(short r, short g, short b, short a)
{
    XRenderColor xrc = {
        .red = (r * 0xffff) / 0xff,
        .green = (g * 0xffff) / 0xff,
        .blue = (b * 0xffff) / 0xff,
        .alpha = (a * 0xffff) / 0xff
    };
    XftColor color;
    if (!XftColorAllocValue(g_display, g_vinfo.visual, g_colormap, &xrc, &color)) {
        std::cerr << "Failed to allocate Xft color" << std::endl;
        exit(1);
    }
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
}

void createOverlayWindow()
{
    XColor bgcolor = createXColorFromRGB(0, 0, 0);
    XMatchVisualInfo(g_display, DefaultScreen(g_display), 32, TrueColor, &g_vinfo);

    g_colormap = XCreateColormap(g_display, DefaultRootWindow(g_display), g_vinfo.visual, AllocNone);
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
                          g_vinfo.depth, InputOutput, g_vinfo.visual, mask, &attr);

    XShapeCombineMask(g_display, g_win, ShapeInput, 0, 0, None, ShapeSet);
    allow_input_passthrough(g_win);
    XMapWindow(g_display, g_win);
}

void initOverlay()
{
    g_screen = DefaultScreen(g_display);
    createOverlayWindow();

    gc = XCreateGC(g_display, g_win, 0, 0);
    
    // Initialize Xft drawing context
    xft_draw = XftDrawCreate(g_display, g_win, g_vinfo.visual, g_colormap);
    
    // Load TTF font using Xft
    xft_font = XftFontOpen(g_display, g_screen,
                          XFT_FAMILY, XftTypeString, "monospace",
                          XFT_SIZE, XftTypeDouble, (double)font_size,
                          NULL);
    
    if (!xft_font) {
        // Fallback to default font if specified font fails
        std::cerr << "Failed to load specified font, using fallback" << std::endl;
        xft_font = XftFontOpen(g_display, g_screen,
                              XFT_FAMILY, XftTypeString, "sans",
                              XFT_SIZE, XftTypeDouble, (double)font_size,
                              NULL);
    }
    if (!xft_font) {
        std::cerr << "Critical error: Could not load any font" << std::endl;
        exit(1);
    }

    // Initialize colors with Xft
    ltblue_xft = createXftColorFromRGBA(0, 255, 255, 255);
    blacka_xft = createXftColorFromRGBA(0, 0, 0, 150);
    white_xft = createXftColorFromRGBA(255, 255, 255, 255);
    outline_xft = createXftColorFromRGBA(0, 0, 0, 255);
}

// Draw plain text with Xft
void drawString(const char *text, int x, int y, XftColor &color, int align)
{
    if (!xft_font || !xft_draw) return;

    XGlyphInfo extents;
    XftTextExtentsUtf8(g_display, xft_font, (const FcChar8*)text, strlen(text), &extents);
    int text_width = extents.width;
    int text_x = x;

    switch(align) {
        case ALIGN_CENTER:
            text_x = x - text_width / 2;
            break;
        case ALIGN_RIGHT:
            text_x = x - text_width;
            break;
        case ALIGN_LEFT:
        default:
            break;
    }

    int baseline = y + xft_font->ascent;
    XftDrawStringUtf8(xft_draw, &color, xft_font, text_x, baseline, (const FcChar8*)text, strlen(text));
}

// Draw text with background
void drawStringBackground(const char *text, int x, int y, XftColor &fg, XftColor &bg, int align, int padding = 4)
{
    if (!xft_font || !xft_draw) return;

    XGlyphInfo extents;
    XftTextExtentsUtf8(g_display, xft_font, (const FcChar8*)text, strlen(text), &extents);
    int text_width = extents.width;
    int text_height = xft_font->ascent + xft_font->descent;
    int rect_width = text_width + 2 * padding;
    int rect_height = text_height + 2 * padding;
    int rect_x = x;
    int text_x = x;

    switch(align) {
        case ALIGN_CENTER:
            rect_x = x - rect_width / 2;
            text_x = x - text_width / 2;
            break;
        case ALIGN_RIGHT:
            rect_x = x - rect_width;
            text_x = x - text_width;
            break;
        case ALIGN_LEFT:
        default:
            rect_x = x;
            text_x = x;
            break;
    }

    // Draw background
    XftDrawRect(xft_draw, &bg, rect_x, y, rect_width, rect_height);
    
    // Draw text
    int baseline = y + padding + xft_font->ascent;
    XftDrawStringUtf8(xft_draw, &fg, xft_font, text_x, baseline, (const FcChar8*)text, strlen(text));
}

// Draw text with outline
void drawStringOutline(const char *text, int x, int y, XftColor &fg, XftColor &outline_color, int align, int outline_thickness = 2)
{
    if (!xft_font || !xft_draw) return;

    XGlyphInfo extents;
    XftTextExtentsUtf8(g_display, xft_font, (const FcChar8*)text, strlen(text), &extents);
    int text_width = extents.width;
    int text_x = x;

    switch(align) {
        case ALIGN_CENTER:
            text_x = x - text_width / 2;
            break;
        case ALIGN_RIGHT:
            text_x = x - text_width;
            break;
        case ALIGN_LEFT:
        default:
            break;
    }

    int baseline = y + xft_font->ascent;

    // Draw outline
    for (int ox = -outline_thickness; ox <= outline_thickness; ox++) {
        for (int oy = -outline_thickness; oy <= outline_thickness; oy++) {
            if (ox != 0 || oy != 0) {
                XftDrawStringUtf8(xft_draw, &outline_color, xft_font, 
                                  text_x + ox, baseline + oy, 
                                  (const FcChar8*)text, strlen(text));
            }
        }
    }
    
    // Draw main text
    XftDrawStringUtf8(xft_draw, &fg, xft_font, text_x, baseline, (const FcChar8*)text, strlen(text));
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

        // Draw timer in multiple positions with different styles
        int vertical_padding = 20;
        int horizontal_padding = 10;
        
        // Top-left: Plain text
        drawString(timer_text, horizontal_padding, vertical_padding, white_xft, ALIGN_LEFT);
        
        // Top-middle: Text with background
        drawStringBackground(timer_text, WIDTH / 2, vertical_padding, white_xft, blacka_xft, ALIGN_CENTER);
        
        // Top-right: Text with outline
        drawStringOutline(timer_text, WIDTH - horizontal_padding, vertical_padding, ltblue_xft, outline_xft, ALIGN_RIGHT);
        
        // Bottom-left: Text with background
        drawStringBackground(timer_text, horizontal_padding, HEIGHT - vertical_padding, white_xft, blacka_xft, ALIGN_LEFT);
        
        // Bottom-middle: Text with outline
        drawStringOutline(timer_text, WIDTH / 2, HEIGHT - vertical_padding, ltblue_xft, outline_xft, ALIGN_CENTER);
        
        // Bottom-right: Plain text
        drawString(timer_text, WIDTH - horizontal_padding, HEIGHT - vertical_padding, white_xft, ALIGN_RIGHT);
        
        // Center: Text with background
        drawStringBackground(timer_text, WIDTH / 2, HEIGHT / 2, ltblue_xft, blacka_xft, ALIGN_CENTER);

        XFlush(g_display);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cleanup (though the program runs indefinitely)
    XftFontClose(g_display, xft_font);
    XftDrawDestroy(xft_draw);
    XftColorFree(g_display, g_vinfo.visual, g_colormap, &ltblue_xft);
    XftColorFree(g_display, g_vinfo.visual, g_colormap, &blacka_xft);
    XftColorFree(g_display, g_vinfo.visual, g_colormap, &white_xft);
    XftColorFree(g_display, g_vinfo.visual, g_colormap, &outline_xft);
    XCloseDisplay(g_display);
    return 0;
}