#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <locale.h>

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

Display *g_display = nullptr;
int g_screen = 0;
Window g_win = 0;
Colormap g_colormap = 0;
GC gc = 0;
XftFont *font = nullptr;          // primary font
XftDraw *xft_draw = nullptr;
Visual *visual = nullptr;
Pixmap back_buffer = 0;
XftDraw *back_draw = nullptr;

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

// ----------------------- UTF-8 helpers & fallback -----------------------

struct TextRun {
    XftFont* f;
    std::string bytes; // UTF-8 bytes for this run
};

static FcConfig* g_fc_config = nullptr;

// cache fallback fonts by their file path (so multiple codepoints sharing the same matched font reuse it)
static std::unordered_map<std::string, XftFont*> g_fallback_cache;

// Decode next UTF-8 codepoint. Returns number of bytes consumed; codepoint in out (UCS-4).
// Invalid sequences yield U+FFFD and advance by 1 byte to avoid infinite loops.
static size_t utf8_next(const char* s, size_t len, size_t i, FcChar32 &out) {
    if (i >= len) return 0;
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) { out = c; return 1; }
    if ((c >> 5) == 0x6 && i + 1 < len) {
        unsigned char c1 = (unsigned char)s[i+1];
        if ((c1 & 0xC0) == 0x80) {
            out = ((c & 0x1F) << 6) | (c1 & 0x3F);
            return 2;
        }
    } else if ((c >> 4) == 0xE && i + 2 < len) {
        unsigned char c1 = (unsigned char)s[i+1], c2 = (unsigned char)s[i+2];
        if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80) {
            out = ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
            return 3;
        }
    } else if ((c >> 3) == 0x1E && i + 3 < len) {
        unsigned char c1 = (unsigned char)s[i+1], c2 = (unsigned char)s[i+2], c3 = (unsigned char)s[i+3];
        if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
            out = ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            return 4;
        }
    }
    out = 0xFFFD; // replacement
    return 1;
}

// Ask fontconfig for a font that contains `ch` at `font_size`.
// Returns an XftFont*, opened and cached. Never returns null; will fall back to primary font.
static XftFont* match_font_for_char(FcChar32 ch) {
    // If primary font has it, use it.
    if (font && XftCharExists(g_display, font, ch)) return font;

    // Build a pattern with desired size and a charset containing `ch`
    FcPattern* pat = FcPatternCreate();
    FcValue v;

    FcPatternAddDouble(pat, FC_SIZE, font_size);

    // include the primary font's file as a weak preference, if we have it (for metrics consistency)
    // Also prefer scalable fonts.
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);

    FcCharSet* cs = FcCharSetCreate();
    FcCharSetAddChar(cs, ch);
    FcPatternAddCharSet(pat, FC_CHARSET, cs);
    FcCharSetDestroy(cs);

    FcConfigSubstitute(g_fc_config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    FcPattern* match = FcFontMatch(g_fc_config, pat, &result);
    FcPatternDestroy(pat);

    if (!match) return font ? font : nullptr;

    // Get the file path for caching
    FcChar8* file = nullptr;
    if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch) {
        XftFont* f = XftFontOpenPattern(g_display, match);
        return f ? f : font;
    }

    std::string key(reinterpret_cast<const char*>(file));
    auto it = g_fallback_cache.find(key);
    if (it != g_fallback_cache.end()) {
        FcPatternDestroy(match); // already cached an opened font, free match
        return it->second;
    }

    XftFont* opened = XftFontOpenPattern(g_display, match); // takes ownership of `match`
    if (!opened) {
        FcPatternDestroy(match);
        return font ? font : nullptr;
    }
    g_fallback_cache.emplace(key, opened);
    return opened;
}

// Break a UTF-8 string into runs of bytes that share the same XftFont* (fallback aware)
static std::vector<TextRun> shape_to_runs(const std::string& s) {
    std::vector<TextRun> runs;
    const char* data = s.c_str();
    size_t len = s.size();
    size_t i = 0;

    XftFont* cur = nullptr;
    std::string cur_bytes;

    while (i < len) {
        FcChar32 cp;
        size_t consumed = utf8_next(data, len, i, cp);
        if (consumed == 0) break;

        XftFont* f = match_font_for_char(cp);
        if (!f) { i += consumed; continue; }

        if (f != cur) {
            if (!cur_bytes.empty()) runs.push_back({cur, cur_bytes});
            cur = f;
            cur_bytes.clear();
        }
        cur_bytes.append(data + i, consumed);
        i += consumed;
    }
    if (!cur_bytes.empty()) runs.push_back({cur, cur_bytes});
    if (runs.empty()) runs.push_back({font, std::string()});
    return runs;
}

// Measure UTF-8 text width (pixels) with fallback-aware runs
static int measure_text_width(const std::string& s) {
    auto runs = shape_to_runs(s);
    int total = 0;
    for (auto& r : runs) {
        XGlyphInfo ext;
        XftTextExtentsUtf8(g_display, r.f, reinterpret_cast<const FcChar8*>(r.bytes.c_str()),
                           (int)r.bytes.size(), &ext);
        total += ext.xOff;
    }
    return total;
}

// ----------------------- X helpers -----------------------

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
    attr.backing_store = Always;

    unsigned long mask = CWColormap | CWBorderPixel | CWBackPixel | CWEventMask |
                         CWWinGravity | CWBitGravity | CWSaveUnder | CWDontPropagate |
                         CWOverrideRedirect | CWBackingStore;

    g_win = XCreateWindow(g_display, DefaultRootWindow(g_display), POSX, POSY, WIDTH, HEIGHT, 0,
                          vinfo.depth, InputOutput, vinfo.visual, mask, &attr);

    back_buffer = XCreatePixmap(g_display, g_win, WIDTH, HEIGHT, vinfo.depth);

    XShapeCombineMask(g_display, g_win, ShapeInput, 0, 0, None, ShapeSet);
    allow_input_passthrough(g_win);
    XMapWindow(g_display, g_win);
}

bool loadTTFFont()
{
    // Initialize FontConfig (once)
    if (!g_fc_config) {
        if (!FcInit()) {
            std::cerr << "Failed to initialize FontConfig" << std::endl;
            return false;
        }
        g_fc_config = FcConfigGetCurrent();
    }

    // Create font pattern from file path
    FcPattern *pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FILE, (const FcChar8*)font_path);
    FcPatternAddDouble(pattern, FC_SIZE, font_size);

    FcConfigSubstitute(g_fc_config, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result;
    FcPattern *match = FcFontMatch(g_fc_config, pattern, &result);
    FcPatternDestroy(pattern);

    if (!match)
    {
        std::cerr << "Failed to find font match for " << font_path << std::endl;
        return false;
    }

    font = XftFontOpenPattern(g_display, match); // takes ownership of `match`
    if (!font)
    {
        std::cerr << "Failed to load TTF font: " << font_path << std::endl;
        return false;
    }

    // Calculate metrics
    font_height = font->ascent + font->descent;

    // For monospace-ish width; use 'M' extents via UTF-8 API
    XGlyphInfo glyph_info;
    const char* M = "M";
    XftTextExtentsUtf8(g_display, font, (const FcChar8*)M, 1, &glyph_info);
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
    ltblue      = createXColorFromRGBA(0,   255, 255, 255);
    blacka      = createXColorFromRGBA(0,   0,   0,   150);
    white       = createXColorFromRGBA(255, 255, 255, 255);
    transparent = createXColorFromRGBA(0,   0,   0,   0);
    outline     = createXColorFromRGBA(0,   0,   0,   255);

    // Initialize Xft colors
    xft_ltblue      = createXftColor(0,   255, 255, 255);
    xft_blacka      = createXftColor(0,   0,   0,   150);
    xft_white       = createXftColor(255, 255, 255, 255);
    xft_transparent = createXftColor(255, 255, 255, 0);
    xft_outline     = createXftColor(0,   0,   0,   255);
}

// ----------------------- Text drawing (UTF-8 + fallback) -----------------------

int getTextWidth(const std::string &text)
{
    return measure_text_width(text);
}

static void draw_runs(const std::vector<TextRun>& runs, int x, int y_baseline, XftColor &fg) {
    int pen_x = x;
    for (const auto& r : runs) {
        XGlyphInfo ext;
        XftTextExtentsUtf8(g_display, r.f, (const FcChar8*)r.bytes.c_str(), (int)r.bytes.size(), &ext);
        XftDrawStringUtf8(back_draw, &fg, r.f, pen_x, y_baseline, (const FcChar8*)r.bytes.c_str(), (int)r.bytes.size());
        pen_x += ext.xOff;
    }
}

static void draw_runs_outline(const std::vector<TextRun>& runs, int x, int y_baseline, XftColor &outline_color, int outline_thickness) {
    int pen_x0 = x;
    for (int ox = -outline_thickness; ox <= outline_thickness; ++ox) {
        for (int oy = -outline_thickness; oy <= outline_thickness; ++oy) {
            if (ox == 0 && oy == 0) continue;
            int pen_x = pen_x0;
            for (const auto& r : runs) {
                XGlyphInfo ext;
                XftTextExtentsUtf8(g_display, r.f, (const FcChar8*)r.bytes.c_str(), (int)r.bytes.size(), &ext);
                XftDrawStringUtf8(back_draw, &outline_color, r.f,
                                  pen_x + ox, y_baseline + oy,
                                  (const FcChar8*)r.bytes.c_str(), (int)r.bytes.size());
                pen_x += ext.xOff;
            }
        }
    }
}

// Plain text
void drawString(const std::string &text, int x, int y, XftColor &fg, int align)
{
    auto runs = shape_to_runs(text);
    int text_width = measure_text_width(text);
    int text_x = x;

    switch (align) {
        case ALIGN_CENTER: text_x = x - text_width / 2; break;
        case ALIGN_RIGHT:  text_x = x - text_width;     break;
        case ALIGN_LEFT:
        default: break;
    }

    int baseline = y + (font ? font->ascent : font_height - (font_height/4));
    draw_runs(runs, text_x, baseline, fg);
}

// Text with background rectangle
void drawStringBackground(const std::string &text, int x, int y, XftColor &fg, XColor &bg, int align, int padding = 4)
{
    auto runs = shape_to_runs(text);
    int text_width = measure_text_width(text);
    int rect_width = text_width + 2 * padding;
    int rect_height = font_height + 2 * padding;
    int rect_x = x;
    int text_x = x;

    switch (align) {
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

    XSetForeground(g_display, gc, bg.pixel);
    XFillRectangle(g_display, back_buffer, gc, rect_x, y, rect_width, rect_height);

    int baseline = y + (font ? font->ascent : font_height - (font_height/4)) + padding;
    draw_runs(runs, text_x, baseline, fg);
}

// Text with outline
void drawStringOutline(const std::string &text, int x, int y, XftColor &fg, XftColor &outline_color, int align, int outline_thickness = 2)
{
    auto runs = shape_to_runs(text);
    int text_width = measure_text_width(text);
    int text_x = x;

    switch (align) {
        case ALIGN_CENTER: text_x = x - text_width / 2; break;
        case ALIGN_RIGHT:  text_x = x - text_width;     break;
        case ALIGN_LEFT:
        default: break;
    }

    int baseline = y + (font ? font->ascent : font_height - (font_height/4));

    // Outline
    draw_runs_outline(runs, text_x, baseline, outline_color, outline_thickness);
    // Fill
    draw_runs(runs, text_x, baseline, fg);
}

// ----------------------- main -----------------------

int main()
{
    // Enable locale so Xlib/Xft understand UTF-8
    setlocale(LC_ALL, "");
    if (!XSupportsLocale()) {
        std::cerr << "Warning: X locale not supported; UTF-8 rendering may fail.\n";
    }
    XSetLocaleModifiers("");

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
        // Keep this as UTF-8. We’ll render via Utf8 APIs with fallback.
        std::string text = std::to_string(elapsed) + " ms🔋↕️↕↕️🧭🛰️⏱⏱⏱🏠";
        const std::string &timer_text = text;

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

        // Copy back buffer to window
        XCopyArea(g_display, back_buffer, g_win, gc, 0, 0, WIDTH, HEIGHT, 0, 0);

        XFlush(g_display);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Cleanup
    if (back_draw) XftDrawDestroy(back_draw);
    if (back_buffer) XFreePixmap(g_display, back_buffer);
    if (xft_draw) XftDrawDestroy(xft_draw);
    if (font) XftFontClose(g_display, font);
    for (auto &kv : g_fallback_cache) {
        if (kv.second && kv.second != font) XftFontClose(g_display, kv.second);
    }
    XCloseDisplay(g_display);
    return 0;
}
