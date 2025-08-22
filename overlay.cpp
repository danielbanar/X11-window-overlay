#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <vector>
#include <string>
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

Display *g_display;
int g_screen;
Window g_win;
Colormap g_colormap;
GC gc;
Visual *visual;
Pixmap back_buffer;
XftDraw *xft_draw;
XftDraw *back_draw;

// --- Fonts & metrics ---
struct FontSet {
    XftFont* primary = nullptr;
    std::vector<XftFont*> fallbacks;
} g_fonts;

const char *font_path = "/home/dan/repos/X11-window-overlay/fonts/consolas.t2tf";
const int font_size = 24;
int line_ascent = 0;   // computed across all open fonts
int line_descent = 0;  // computed across all open fonts
int font_height = 24;  // computed from ascent+descent

// Colors
XColor ltblue, blacka, transparent, white, outline;
XftColor xft_ltblue, xft_blacka, xft_white, xft_outline, xft_transparent;

int WIDTH = 640;
int HEIGHT = 480;
int POSX = 0;
int POSY = 0;

// Precomputed text metrics cache
struct TextMetrics {
    std::vector<std::pair<XftFont*, std::string>> runs;
    int width;
};

// ---------- Scoped Timer ----------
struct ScopeTimer {
    const char* name;
    std::chrono::steady_clock::time_point start;
    inline static long long threshold_ms = 5; // only print if >1 ms

    ScopeTimer(const char* n) : name(n), start(std::chrono::steady_clock::now()) {}
    ~ScopeTimer() {
        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        if (ms >= threshold_ms) {
            std::cout << "[TIMER] " << name << " executed in " << ms << " ms\n";
        }
    }
};

// ---------- helpers ----------
static inline bool utf8_next(const char* s, int len, int& i, FcChar32& out)
{
    if (i >= len) return false;
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) { out = c; return true; }
    if ((c >> 5) == 0x6 && i < len) { out = ((c & 0x1F) << 6) | (s[i++] & 0x3F); return true; }
    if ((c >> 4) == 0xE && i + 1 < len) { out = ((c & 0x0F) << 12) | ((s[i++] & 0x3F) << 6) | (s[i++] & 0x3F); return true; }
    if ((c >> 3) == 0x1E && i + 2 < len) { out = ((c & 0x07) << 18) | ((s[i++] & 0x3F) << 12) | ((s[i++] & 0x3F) << 6) | (s[i++] & 0x3F); return true; }
    out = 0xFFFD;
    return true;
}

XColor createXColorFromRGB(short r, short g, short b)
{
    ScopeTimer timer("createXColorFromRGB");
    XColor color;
    color.red   = (r * 0xFFFF) / 0xFF;
    color.green = (g * 0xFFFF) / 0xFF;
    color.blue  = (b * 0xFFFF) / 0xFF;
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
    ScopeTimer timer("createXColorFromRGBA");
    XColor color = createXColorFromRGB(r, g, b);
    *(&color.pixel) = ((*(&color.pixel)) & 0x00ffffff) | (a << 24);
    return color;
}

XftColor createXftColor(short r, short g, short b, short a = 255)
{
    ScopeTimer timer("createXftColor");
    XftColor color;
    XRenderColor render_color;
    render_color.red   = (r * 0xFFFF) / 0xFF;
    render_color.green = (g * 0xFFFF) / 0xFF;
    render_color.blue  = (b * 0xFFFF) / 0xFF;
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
    ScopeTimer timer("allow_input_passthrough");
    XserverRegion region = XFixesCreateRegion(g_display, NULL, 0);
    XFixesSetWindowShapeRegion(g_display, w, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(g_display, region);
}

bool findWindowByClass(Window root, const std::string &target_class, Window &outWin)
{
    ScopeTimer timer("findWindowByClass");
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
                    if (cls == target_class) match = true;
                }
                if (classHint.res_name)  XFree(classHint.res_name);
                if (classHint.res_class) XFree(classHint.res_class);
                if (match)
                {
                    outWin = children[i];
                    if (children) XFree(children);
                    return true;
                }
            }
            if (findWindowByClass(children[i], target_class, outWin))
            {
                if (children) XFree(children);
                return true;
            }
        }
        if (children) XFree(children);
    }
    return false;
}

void getWindowGeometry(Window win)
{
    ScopeTimer timer("getWindowGeometry");
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
    ScopeTimer timer("createOverlayWindow");
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

// --- font loading & fallback ---
static XftFont* openFontFromFile(const char* path, double size)
{
    ScopeTimer timer("openFontFromFile");
    FcPattern* pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FILE, (const FcChar8*)path);
    FcPatternAddDouble(pattern, FC_SIZE, size);

    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result;
    FcPattern* match = FcFontMatch(nullptr, pattern, &result);
    FcPatternDestroy(pattern);
    if (!match) return nullptr;

    return XftFontOpenPattern(g_display, match);
}

static XftFont* openFontByFamily(const char* family, double size)
{
    ScopeTimer timer("openFontByFamily");
    FcPattern* pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)family);
    FcPatternAddDouble(pat, FC_SIZE, size);
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    FcPattern* match = FcFontMatch(nullptr, pat, &result);
    FcPatternDestroy(pat);
    if (!match) return nullptr;

    return XftFontOpenPattern(g_display, match);
}

static void computeLineMetrics()
{
    ScopeTimer timer("computeLineMetrics");
    line_ascent = 0;
    line_descent = 0;
    if (g_fonts.primary) {
        line_ascent  = std::max(line_ascent,  g_fonts.primary->ascent);
        line_descent = std::max(line_descent, g_fonts.primary->descent);
    }
    for (auto* f : g_fonts.fallbacks) {
        if (!f) continue;
        line_ascent  = std::max(line_ascent,  f->ascent);
        line_descent = std::max(line_descent, f->descent);
    }
    font_height = line_ascent + line_descent;
}

static bool loadFonts()
{
    ScopeTimer timer("loadFonts");
    if (!FcInit()) {
        std::cerr << "Failed to initialize FontConfig\n";
        return false;
    }

    g_fonts.primary = openFontFromFile(font_path, font_size);
    if (!g_fonts.primary) {
        std::cerr << "Failed to load primary font: " << font_path << "\n";
        return false;
    }

    const char* fallbackFamilies[] = {
        "Noto Color Emoji","Noto Emoji","EmojiOne Color","Twitter Color Emoji",
        "Segoe UI Symbol","Symbola","DejaVu Sans","DejaVu Sans Mono","Liberation Sans"
    };

    for (const char* fam : fallbackFamilies) {
        if (auto* f = openFontByFamily(fam, font_size)) {
            g_fonts.fallbacks.push_back(f);
        }
    }

    computeLineMetrics();

    std::cout << "Loaded base font: " << font_path
              << " size " << font_size
              << " + " << g_fonts.fallbacks.size() << " fallback(s)\n";
    return true;
}

static XftFont* pickFontForChar(FcChar32 ch)
{
    ScopeTimer timer("pickFontForChar");
    if (g_fonts.primary && XftCharExists(g_display, g_fonts.primary, ch))
        return g_fonts.primary;

    for (auto* f : g_fonts.fallbacks)
        if (f && XftCharExists(g_display, f, ch))
            return f;

    return g_fonts.primary;
}

// group UTF-8 into runs
static void utf8ToFontRuns(const char* text, int len,
                           std::vector<std::pair<XftFont*, std::string>>& runs)
{
    ScopeTimer timer("utf8ToFontRuns");
    int i = 0;
    XftFont* current = nullptr;
    std::string buf;

    while (i < len) {
        FcChar32 cp;
        int before = i;
        utf8_next(text, len, i, cp);
        XftFont* f = pickFontForChar(cp);

        if (current == nullptr) {
            current = f;
        }
        if (f != current) {
            if (!buf.empty()) runs.push_back({current, buf});
            buf.clear();
            current = f;
        }
        buf.append(text + before, i - before);
    }
    if (!buf.empty() && current) runs.push_back({current, buf});
}

// Precompute text metrics (runs and width)
static TextMetrics computeTextMetrics(const char* text)
{
    ScopeTimer timer("computeTextMetrics");
    TextMetrics tm;
    int len = (int)strlen(text);
    utf8ToFontRuns(text, len, tm.runs);
    
    tm.width = 0;
    for (auto& r : tm.runs) {
        XGlyphInfo gi;
        XftTextExtentsUtf8(g_display, r.first, (const FcChar8*)r.second.c_str(), (int)r.second.size(), &gi);
        tm.width += gi.xOff;
    }
    return tm;
}

// Fast drawing of precomputed text runs
static void drawTextRuns(XftDraw* draw, const TextMetrics& tm, int x, int baselineY, const XftColor* col)
{
    ScopeTimer timer("drawTextRuns");
    int penX = x;
    for (auto& r : tm.runs) {
        XGlyphInfo gi;
        XftTextExtentsUtf8(g_display, r.first, (const FcChar8*)r.second.c_str(), (int)r.second.size(), &gi);
        XftDrawStringUtf8(draw, col, r.first, penX, baselineY, (const FcChar8*)r.second.c_str(), (int)r.second.size());
        penX += gi.xOff;
    }
}

// Optimized outline drawing using precomputed metrics
static void drawTextRunsOutline(XftDraw* draw, const TextMetrics& tm, int x, int baselineY, 
                               const XftColor* fg, const XftColor* outline_color, int outline_thickness = 2)
{
    ScopeTimer timer("drawTextRunsOutline");
    // Draw outline first (only the outer pixels, not the full 3x3 grid)
    const int offsets[8][2] = {{-1,-1}, {-1,0}, {-1,1}, {0,-1}, {0,1}, {1,-1}, {1,0}, {1,1}};
    
    for (int i = 0; i < 8; i++) {
        int penX = x + offsets[i][0] * outline_thickness;
        int penY = baselineY + offsets[i][1] * outline_thickness;
        
        for (auto& r : tm.runs) {
            XGlyphInfo gi;
            XftTextExtentsUtf8(g_display, r.first, (const FcChar8*)r.second.c_str(), (int)r.second.size(), &gi);
            XftDrawStringUtf8(draw, outline_color, r.first, penX, penY, (const FcChar8*)r.second.c_str(), (int)r.second.size());
            penX += gi.xOff;
        }
    }
    
    // Draw main text
    int penX = x;
    for (auto& r : tm.runs) {
        XGlyphInfo gi;
        XftTextExtentsUtf8(g_display, r.first, (const FcChar8*)r.second.c_str(), (int)r.second.size(), &gi);
        XftDrawStringUtf8(draw, fg, r.first, penX, baselineY, (const FcChar8*)r.second.c_str(), (int)r.second.size());
        penX += gi.xOff;
    }
}

// --- overlay init ---
void initOverlay()
{
    ScopeTimer timer("initOverlay");
    g_screen = DefaultScreen(g_display);
    createOverlayWindow();

    gc = XCreateGC(g_display, g_win, 0, 0);

    setlocale(LC_CTYPE, "");
    if (!XSupportsLocale()) {
        std::cerr << "Warning: X does not support current locale\n";
    }
    XSetLocaleModifiers("");

    if (!loadFonts()) {
        std::cerr << "Failed to load fonts, exiting...\n";
        exit(1);
    }

    xft_draw = XftDrawCreate(g_display, g_win, visual, g_colormap);
    back_draw = XftDrawCreate(g_display, back_buffer, visual, g_colormap);

    ltblue      = createXColorFromRGBA(0,   255, 255, 255);
    blacka      = createXColorFromRGBA(0,   0,   0,   150);
    white       = createXColorFromRGBA(255, 255, 255, 255);
    transparent = createXColorFromRGBA(0,   0,   0,   0);
    outline     = createXColorFromRGBA(0,   0,   0,   255);

    xft_ltblue     = createXftColor(0,   255, 255, 255);
    xft_blacka     = createXftColor(0,   0,   0,   150);
    xft_white      = createXftColor(255, 255, 255, 255);
    xft_transparent= createXftColor(255, 255, 255, 0);
    xft_outline    = createXftColor(0,   0,   0,   255);
}

// --- drawString wrappers ---
void drawString(const TextMetrics& tm, int x, int y, XftColor &fg, int align)
{
    ScopeTimer timer("drawString");
    int text_x = x;
    switch(align) {
        case ALIGN_CENTER: text_x = x - tm.width / 2; break;
        case ALIGN_RIGHT:  text_x = x - tm.width;     break;
        case ALIGN_LEFT:
        default: break;
    }
    int baseline = y + line_ascent;
    drawTextRuns(back_draw, tm, text_x, baseline, &fg);
}

void drawStringBackground(const TextMetrics& tm, int x, int y, XftColor &fg, XColor &bg, int align, int padding = 4)
{
    ScopeTimer timer("drawStringBackground");
    int rect_width = tm.width + 2 * padding;
    int rect_height = font_height + 2 * padding;

    int rect_x = x;
    int text_x = x;

    switch(align) {
        case ALIGN_CENTER:
            rect_x = x - rect_width / 2;
            text_x = x - tm.width / 2;
            break;
        case ALIGN_RIGHT:
            rect_x = x - rect_width;
            text_x = x - tm.width;
            break;
        case ALIGN_LEFT:
        default:
            break;
    }

    XSetForeground(g_display, gc, bg.pixel);
    XFillRectangle(g_display, back_buffer, gc, rect_x, y, rect_width, rect_height);

    int baseline = y + padding + line_ascent;
    drawTextRuns(back_draw, tm, text_x, baseline, &fg);
}

// --- optimized outline version ---
void drawStringOutline(const TextMetrics& tm, int x, int y, XftColor &fg, XftColor &outline_color, int align, int outline_thickness = 2)
{
    ScopeTimer timer("drawStringOutline");
    int text_x = x;
    switch(align) {
        case ALIGN_CENTER: text_x = x - tm.width / 2; break;
        case ALIGN_RIGHT:  text_x = x - tm.width;     break;
        case ALIGN_LEFT:
        default: break;
    }

    int baseline = y + line_ascent;
    drawTextRunsOutline(back_draw, tm, text_x, baseline, &fg, &outline_color, outline_thickness);
}

// --- main loop ---
int main()
{
    ScopeTimer main_timer("Main Loop");
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
        ScopeTimer frame_timer("Frame");
        getWindowGeometry(target);

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

        // Clear back buffer
        XSetForeground(g_display, gc, transparent.pixel);
        XFillRectangle(g_display, back_buffer, gc, 0, 0, WIDTH, HEIGHT);

        // Elapsed time text
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        std::string text = std::to_string(elapsed) + " ms 🔋 ↕️ ↕ 🧭 🛰️ ⏱ 🏠";
        const char* timer_text = text.c_str();

        // Precompute text metrics once per frame
        TextMetrics tm = computeTextMetrics(timer_text);

        int vertical_padding = 20;
        int horizontal_padding = 10;

        drawString(tm, horizontal_padding, vertical_padding, xft_white, ALIGN_LEFT);
        drawStringBackground(tm, WIDTH / 2, vertical_padding, xft_white, blacka, ALIGN_CENTER);
        drawStringOutline(tm, WIDTH - horizontal_padding, vertical_padding, xft_ltblue, xft_outline, ALIGN_RIGHT);
        drawStringBackground(tm, horizontal_padding, HEIGHT - vertical_padding - font_height, xft_white, blacka, ALIGN_LEFT);
        drawStringOutline(tm, WIDTH / 2, HEIGHT - vertical_padding - font_height, xft_ltblue, xft_outline, ALIGN_CENTER);
        drawString(tm, WIDTH - horizontal_padding, HEIGHT - vertical_padding - font_height, xft_white, ALIGN_RIGHT);
        drawStringBackground(tm, WIDTH / 2, HEIGHT / 2 - font_height / 2, xft_ltblue, blacka, ALIGN_CENTER);

        XCopyArea(g_display, back_buffer, g_win, gc, 0, 0, WIDTH, HEIGHT, 0, 0);
        XFlush(g_display);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (back_draw) XftDrawDestroy(back_draw);
    if (back_buffer) XFreePixmap(g_display, back_buffer);
    if (xft_draw) XftDrawDestroy(xft_draw);

    if (g_fonts.primary) XftFontClose(g_display, g_fonts.primary);
    for (auto* f : g_fonts.fallbacks) if (f) XftFontClose(g_display, f);

    XCloseDisplay(g_display);
    return 0;
}