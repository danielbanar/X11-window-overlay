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
#include <X11/Xft/Xft.h>
#include <fontconfig/fontconfig.h>

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
XftFont *font;
XftDraw *xft_draw;
Visual *visual;
Pixmap back_buffer;
XftDraw *back_draw;

// Font configuration
const char *font_path = "/home/dan/repos/X11-window-overlay/fonts/consolas.t2tf";
const int font_size = 24;
int font_width = 12;  // Will be calculated dynamically
int font_height = 24; // Will be calculated dynamically

// Colors
XColor ltblue, blacka, transparent, white, outline;
XftColor xft_ltblue, xft_blacka, xft_white, xft_outline, xft_transparent;

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

XftColor createXftColor(short r, short g, short b, short a = 255)
{
    XftColor color;
    XRenderColor render_color;
    render_color.red = (r * 0xFFFF) / 0xFF;
    render_color.green = (g * 0xFFFF) / 0xFF;
    render_color.blue = (b * 0xFFFF) / 0xFF;
    render_color.alpha = (a * 0xFFFF) / 0xFF;
    
    if (!XftColorAllocValue(g_display, visual, g_colormap, &render_color, &color))
    {
        std::cerr << "Cannot create Xft color" << std::endl;
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
    XColor bgcolor = createXColorFromRGBA(0, 0, 0, 0);
    XVisualInfo vinfo;
    XMatchVisualInfo(g_display, DefaultScreen(g_display), 32, TrueColor, &vinfo);
    visual = vinfo.visual;

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
    attr.backing_store = Always;  // Enable backing store

    unsigned long mask = CWColormap | CWBorderPixel | CWBackPixel | CWEventMask |
                         CWWinGravity | CWBitGravity | CWSaveUnder | CWDontPropagate | 
                         CWOverrideRedirect | CWBackingStore;

    g_win = XCreateWindow(g_display, DefaultRootWindow(g_display), POSX, POSY, WIDTH, HEIGHT, 0,
                          vinfo.depth, InputOutput, vinfo.visual, mask, &attr);

    // Create double buffer
    back_buffer = XCreatePixmap(g_display, g_win, WIDTH, HEIGHT, vinfo.depth);

    XShapeCombineMask(g_display, g_win, ShapeInput, 0, 0, None, ShapeSet);
    allow_input_passthrough(g_win);
    XMapWindow(g_display, g_win);
}

bool loadTTFFont()
{
    // Initialize FontConfig
    if (!FcInit())
    {
        std::cerr << "Failed to initialize FontConfig" << std::endl;
        return false;
    }

    // Create font pattern from file path
    FcPattern *pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FILE, (const FcChar8*)font_path);
    FcPatternAddDouble(pattern, FC_SIZE, font_size);
    
    // Configure the pattern
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    
    // Find the best match
    FcResult result;
    FcPattern *match = FcFontMatch(NULL, pattern, &result);
    
    if (!match)
    {
        std::cerr << "Failed to find font match for " << font_path << std::endl;
        FcPatternDestroy(pattern);
        return false;
    }
    
    // Load the font using Xft
    font = XftFontOpenPattern(g_display, match);
    FcPatternDestroy(pattern);
    
    if (!font)
    {
        std::cerr << "Failed to load TTF font: " << font_path << std::endl;
        return false;
    }
    
    // Calculate font metrics
    font_height = font->ascent + font->descent;
    font_width = font->max_advance_width;
    
    // For monospace fonts, we can get a more accurate width
    XGlyphInfo glyph_info;
    XftTextExtents8(g_display, font, (FcChar8*)"M", 1, &glyph_info);
    font_width = glyph_info.xOff;
    
    std::cout << "Loaded TTF font: " << font_path 
              << " (size: " << font_size 
              << ", width: " << font_width 
              << ", height: " << font_height << ")" << std::endl;
    
    return true;
}

void initOverlay()
{
    g_screen = DefaultScreen(g_display);
    createOverlayWindow();

    gc = XCreateGC(g_display, g_win, 0, 0);
    
    // Load TTF font
    if (!loadTTFFont())
    {
        std::cerr << "Failed to load TTF font, exiting..." << std::endl;
        exit(1);
    }
    
    // Create Xft draw contexts for both window and back buffer
    xft_draw = XftDrawCreate(g_display, g_win, visual, g_colormap);
    back_draw = XftDrawCreate(g_display, back_buffer, visual, g_colormap);
    
    // Initialize colors
    ltblue = createXColorFromRGBA(0, 255, 255, 255);
    blacka = createXColorFromRGBA(0, 0, 0, 150);
    white = createXColorFromRGBA(255, 255, 255, 255);
    transparent = createXColorFromRGBA(0, 0, 0, 0);
    outline = createXColorFromRGBA(0, 0, 0, 255);
    
    // Initialize Xft colors
    xft_ltblue = createXftColor(0, 255, 255, 255);
    xft_blacka = createXftColor(0, 0, 0, 150);
    xft_white = createXftColor(255, 255, 255, 255);
    xft_transparent = createXftColor(255, 255, 255, 0);
    xft_outline = createXftColor(0, 0, 0, 255);
}

int getTextWidth(const char *text)
{
    XGlyphInfo glyph_info;
    XftTextExtents8(g_display, font, (FcChar8*)text, strlen(text), &glyph_info);
    return glyph_info.width;
}

// Draw plain text without any background or outline
void drawString(const char *text, int x, int y, XftColor &fg, int align)
{
    int text_width = getTextWidth(text);
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
            break;
    }

    XftDrawString8(back_draw, &fg, font, text_x, y + font->ascent, (FcChar8*)text, strlen(text));
}

// Draw text with a background rectangle
void drawStringBackground(const char *text, int x, int y, XftColor &fg, XColor &bg, int align, int padding = 4)
{
    int text_width = getTextWidth(text);
    int rect_width = text_width + 2 * padding;
    int rect_height = font_height + 2 * padding;
    int rect_x = x;
    int text_x = x;

    // Adjust positions based on alignment
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
            break;
    }

    // Draw background rectangle on back buffer
    XSetForeground(g_display, gc, bg.pixel);
    XFillRectangle(g_display, back_buffer, gc, rect_x, y, rect_width, rect_height);
    
    // Draw text on back buffer
    XftDrawString8(back_draw, &fg, font, text_x, y + font->ascent + padding, (FcChar8*)text, strlen(text));
}

// Draw text with an outline
void drawStringOutline(const char *text, int x, int y, XftColor &fg, XftColor &outline_color, int align, int outline_thickness = 2)
{
    int text_width = getTextWidth(text);
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
            break;
    }

    // Draw outline on back buffer
    for (int ox = -outline_thickness; ox <= outline_thickness; ox++) {
        for (int oy = -outline_thickness; oy <= outline_thickness; oy++) {
            if (ox != 0 || oy != 0) {
                XftDrawString8(back_draw, &outline_color, font, 
                             text_x + ox, 
                             y + font->ascent + oy, 
                             (FcChar8*)text, strlen(text));
            }
        }
    }
    
    // Draw main text on back buffer
    XftDrawString8(back_draw, &fg, font, text_x, y + font->ascent, (FcChar8*)text, strlen(text));
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
    int last_width = WIDTH, last_height = HEIGHT;

    while (true)
    {
        // Recalculate GStreamer window position & size
        getWindowGeometry(target);

        // Move & resize overlay if needed
        bool size_changed = (WIDTH != last_width || HEIGHT != last_height);
        if (size_changed) {
            XMoveResizeWindow(g_display, g_win, POSX, POSY, WIDTH, HEIGHT);
            
            // Recreate back buffer with new size
            if (back_buffer) XFreePixmap(g_display, back_buffer);
            if (back_draw) XftDrawDestroy(back_draw);
            
            XVisualInfo vinfo;
            XMatchVisualInfo(g_display, DefaultScreen(g_display), 32, TrueColor, &vinfo);
            back_buffer = XCreatePixmap(g_display, g_win, WIDTH, HEIGHT, vinfo.depth);
            back_draw = XftDrawCreate(g_display, back_buffer, visual, g_colormap);
            
            last_width = WIDTH;
            last_height = HEIGHT;
        } else {
            XMoveWindow(g_display, g_win, POSX, POSY);
        }

        // Clear the back buffer
        XSetForeground(g_display, gc, transparent.pixel);
        XFillRectangle(g_display, back_buffer, gc, 0, 0, WIDTH, HEIGHT);

        // Get elapsed time
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        std::string text = std::to_string(elapsed) + " ms";
        const char* timer_text = text.c_str();

        // Draw timer in multiple positions with different styles
        int vertical_padding = 20;
        int horizontal_padding = 10;
        
        // Top-left: Plain text
        drawString(timer_text, horizontal_padding, vertical_padding, xft_white, ALIGN_LEFT);
        
        // Top-middle: Text with background
        drawStringBackground(timer_text, WIDTH / 2, vertical_padding, xft_white, blacka, ALIGN_CENTER);
        
        // Top-right: Text with outline
        drawStringOutline(timer_text, WIDTH - horizontal_padding, vertical_padding, xft_ltblue, xft_outline, ALIGN_RIGHT);
        
        // Bottom-left: Text with background
        drawStringBackground(timer_text, horizontal_padding, HEIGHT - vertical_padding, xft_white, blacka, ALIGN_LEFT);
        
        // Bottom-middle: Text with outline
        drawStringOutline(timer_text, WIDTH / 2, HEIGHT - vertical_padding, xft_ltblue, xft_outline, ALIGN_CENTER);
        
        // Bottom-right: Plain text
        drawString(timer_text, WIDTH - horizontal_padding, HEIGHT - vertical_padding, xft_white, ALIGN_RIGHT);
        
        // Center: Text with background
        drawStringBackground(timer_text, WIDTH / 2, HEIGHT / 2, xft_ltblue, blacka, ALIGN_CENTER);

        // Copy back buffer to window in one atomic operation
        XCopyArea(g_display, back_buffer, g_win, gc, 0, 0, WIDTH, HEIGHT, 0, 0);
        
        XFlush(g_display);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cleanup
    if (back_draw) XftDrawDestroy(back_draw);
    if (back_buffer) XFreePixmap(g_display, back_buffer);
    if (xft_draw) XftDrawDestroy(xft_draw);
    if (font) XftFontClose(g_display, font);
    XCloseDisplay(g_display);
    return 0;
}