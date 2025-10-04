#include "draw.h"
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#define COMPENSATE_SIZE

#ifdef COMPENSATE_SIZE
const double FONT_SIZE_COMPENSATION = 20.0 / 16.0;
#else
const double FONT_SIZE_COMPENSATION = 1.0;
#endif

#define BASIC_EVENT_MASK (StructureNotifyMask | ExposureMask | PropertyChangeMask | EnterWindowMask | LeaveWindowMask | KeyPressMask | KeyReleaseMask | KeymapStateMask)
#define NOT_PROPAGATE_MASK (KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ButtonMotionMask)

namespace
{
    Display* display = nullptr;
    int screen = 0;
    Window target_window = 0;
    Window overlay_window = 0;
    Colormap colormap = 0;
    Visual* visual = nullptr;
    Pixmap back_buffer = 0;
    XftDraw* back_draw = nullptr;
    GC gc = nullptr;

    int width = 0;
    int height = 0;
    int pos_x = 0;
    int pos_y = 0;

    bool overlay_initialized = false;
    std::string current_window_class;
    bool colors_initialized = false;

    struct FontSet
    {
        XftFont* primary = nullptr;
        std::vector<XftFont*> fallbacks;
        int line_ascent = 0;
        int line_descent = 0;
        int font_height = 0;
    };

    struct FontCacheEntry
    {
        std::string family;
        int size;
        FontSet font_set;
    };

    std::vector<FontCacheEntry> font_cache;

    XftColor xft_white, xft_black, xft_ltblue, xft_outline;

    bool utf8_next(const char* s, int len, int& i, FcChar32& out)
    {
        if (i >= len)
            return false;
        unsigned char c = (unsigned char)s[i++];
        if (c < 0x80)
        {
            out = c;
            return true;
        }
        if ((c >> 5) == 0x6 && i < len)
        {
            out = ((c & 0x1F) << 6) | (s[i++] & 0x3F);
            return true;
        }
        if ((c >> 4) == 0xE && i + 1 < len)
        {
            out = ((c & 0x0F) << 12) | ((s[i++] & 0x3F) << 6) | (s[i++] & 0x3F);
            return true;
        }
        if ((c >> 3) == 0x1E && i + 2 < len)
        {
            out = ((c & 0x07) << 18) | ((s[i++] & 0x3F) << 12) | ((s[i++] & 0x3F) << 6) | (s[i++] & 0x3F);
            return true;
        }
        out = 0xFFFD;
        return true;
    }

    XftColor createXftColor(double r, double g, double b, double a = 1.0)
    {
        XRenderColor rc;
        rc.red = static_cast<unsigned short>(r * 65535.0);
        rc.green = static_cast<unsigned short>(g * 65535.0);
        rc.blue = static_cast<unsigned short>(b * 65535.0);
        rc.alpha = static_cast<unsigned short>(a * 65535.0);

        XftColor color;
        if (!XftColorAllocValue(display, visual, colormap, &rc, &color))
        {
            std::cerr << "Cannot create Xft color\n";
            // Return a default color instead of aborting
            color.pixel = 0;
            color.color.red = 0;
            color.color.green = 0;
            color.color.blue = 0;
            color.color.alpha = 65535;
        }
        return color;
    }

    unsigned long rgba_to_pixel(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
    {
        return (static_cast<unsigned long>(a) << 24) |
               (static_cast<unsigned long>(r) << 16) |
               (static_cast<unsigned long>(g) << 8) |
               (static_cast<unsigned long>(b) << 0);
    }

    void allowInputPassthrough(Window w)
    {
        XserverRegion region = XFixesCreateRegion(display, NULL, 0);
        XFixesSetWindowShapeRegion(display, w, ShapeInput, 0, 0, region);
        XFixesDestroyRegion(display, region);
    }

    bool findWindowByClass(Window root, const std::string& target_class, Window& outWin)
    {
        Window root_return, parent_return;
        Window* children = nullptr;
        unsigned int nchildren = 0;

        if (XQueryTree(display, root, &root_return, &parent_return, &children, &nchildren))
        {
            for (unsigned int i = 0; i < nchildren; ++i)
            {
                XClassHint classHint;
                if (XGetClassHint(display, children[i], &classHint))
                {
                    bool match = false;
                    if (classHint.res_class && std::string(classHint.res_class) == target_class)
                    {
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
        if (!display || !win)
            return;

        XWindowAttributes attr;
        if (!XGetWindowAttributes(display, win, &attr))
        {
            std::cerr << "Failed to get window attributes, window may have closed\n";
            return;
        }

        Window child;
        int x, y;
        if (!XTranslateCoordinates(display, win, DefaultRootWindow(display), 0, 0, &x, &y, &child))
        {
            std::cerr << "Failed to translate window coordinates\n";
            return;
        }

        pos_x = x;
        pos_y = y;
        width = attr.width;
        height = attr.height;
    }

    XftFont* openFontByFamily(const char* family, double size)
    {
        FcPattern* pat = FcPatternCreate();
        FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)family);
        FcPatternAddDouble(pat, FC_SIZE, size);
        FcConfigSubstitute(nullptr, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);

        FcResult result;
        FcPattern* match = FcFontMatch(nullptr, pat, &result);
        FcPatternDestroy(pat);
        if (!match)
            return nullptr;

        return XftFontOpenPattern(display, match);
    }

    FontSet* getFontSet(const char* font_family, int font_size)
    {
        // Use default values if not specified
        const char* family = font_family ? font_family : "Consolas";
        int size = font_size > 0 ? 
#ifdef COMPENSATE_SIZE
    static_cast<int>(font_size * FONT_SIZE_COMPENSATION)
#else
    font_size
#endif
    : 
#ifdef COMPENSATE_SIZE
    25
#else
    20
#endif
    ;

        // Check if we already have this font cached
        for (auto& entry : font_cache)
        {
            if (entry.family == family && entry.size == size)
            {
                return &entry.font_set;
            }
        }

        // Create new font set
        FontCacheEntry new_entry;
        new_entry.family = family;
        new_entry.size = size;

        // Load primary font
        new_entry.font_set.primary = openFontByFamily(family, size);
        if (!new_entry.font_set.primary)
        {
            std::cerr << "Failed to load primary font: " << family << "\n";
            // Fallback to default font
            new_entry.font_set.primary = openFontByFamily("Consolas", 20);
        }

        // Load fallback fonts
        const char* fallbackFamilies[] = {
            "Noto Color Emoji", "Noto Emoji", "EmojiOne Color", "Twitter Color Emoji",
            "Segoe UI Symbol", "Symbola", "DejaVu Sans", "DejaVu Sans Mono", "Liberation Sans"};
        for (const char* fam : fallbackFamilies)
        {
            if (auto* f = openFontByFamily(fam, size))
            {
                new_entry.font_set.fallbacks.push_back(f);
            }
        }

        // Compute line metrics
        new_entry.font_set.line_ascent = 0;
        new_entry.font_set.line_descent = 0;
        if (new_entry.font_set.primary)
        {
            new_entry.font_set.line_ascent = std::max(new_entry.font_set.line_ascent, new_entry.font_set.primary->ascent);
            new_entry.font_set.line_descent = std::max(new_entry.font_set.line_descent, new_entry.font_set.primary->descent);
        }
        for (auto* f : new_entry.font_set.fallbacks)
        {
            if (!f)
                continue;
            new_entry.font_set.line_ascent = std::max(new_entry.font_set.line_ascent, f->ascent);
            new_entry.font_set.line_descent = std::max(new_entry.font_set.line_descent, f->descent);
        }
        new_entry.font_set.font_height = new_entry.font_set.line_ascent + new_entry.font_set.line_descent;

        font_cache.push_back(new_entry);
        return &font_cache.back().font_set;
    }

    XftFont* pickFontForChar(FontSet* font_set, FcChar32 ch)
    {
        if (font_set->primary && XftCharExists(display, font_set->primary, ch))
            return font_set->primary;

        for (auto* f : font_set->fallbacks)
            if (f && XftCharExists(display, f, ch))
                return f;

        return font_set->primary;
    }

    struct TextMetrics
    {
        std::vector<std::pair<XftFont*, std::string>> runs;
        std::vector<int> line_widths;
        int width = 0;
        int height = 0;
    };

    std::map<std::string, TextMetrics> textMetricsCache;

    void utf8ToFontRuns(const char* text, int len, std::vector<std::pair<XftFont*, std::string>>& runs, FontSet* font_set)
    {
        int i = 0;
        XftFont* current = nullptr;
        std::string buf;

        while (i < len)
        {
            FcChar32 cp;
            int before = i;
            utf8_next(text, len, i, cp);

            if (cp == '\n')
            {
                if (!buf.empty() && current)
                {
                    runs.push_back({current, buf});
                    buf.clear();
                }
                runs.push_back({nullptr, "\n"});
                current = nullptr;
                continue;
            }

            XftFont* f = pickFontForChar(font_set, cp);

            if (current == nullptr)
                current = f;
            if (f != current)
            {
                if (!buf.empty())
                    runs.push_back({current, buf});
                buf.clear();
                current = f;
            }
            buf.append(text + before, i - before);
        }
        if (!buf.empty() && current)
            runs.push_back({current, buf});
    }

    TextMetrics computeTextMetrics(const std::string& text, FontSet* font_set)
    {
        auto it = textMetricsCache.find(text);
        if (it != textMetricsCache.end())
            return it->second;

        TextMetrics tm;
        utf8ToFontRuns(text.c_str(), static_cast<int>(text.size()), tm.runs, font_set);

        tm.line_widths.clear();
        int current_line_width = 0;
        int line_count = 1;

        for (auto& r : tm.runs)
        {
            if (r.first == nullptr && r.second == "\n")
            {
                tm.line_widths.push_back(current_line_width);
                current_line_width = 0;
                line_count++;
                continue;
            }

            XGlyphInfo gi;
            XftTextExtentsUtf8(display, r.first,
                               (const FcChar8*)r.second.c_str(),
                               (int)r.second.size(), &gi);
            current_line_width += gi.xOff;
        }

        // Add the last line
        tm.line_widths.push_back(current_line_width);

        // Find the maximum line width
        tm.width = 0;
        for (int w : tm.line_widths)
        {
            if (w > tm.width)
                tm.width = w;
        }

        tm.height = line_count * font_set->font_height;

        textMetricsCache.emplace(text, tm);
        return tm;
    }

    void drawTextRuns(const TextMetrics& tm, int x, int baselineY, const XftColor* col,
                      FontSet* font_set, Draw::TextAlignment alignment)
    {
        int line_index = 0;
        int penX = x;
        int penY = baselineY;

        // Adjust initial X position based on alignment for the first line
        if (alignment != Draw::ALIGN_LEFT && !tm.line_widths.empty())
        {
            if (alignment == Draw::ALIGN_CENTER)
                penX = x - tm.line_widths[0] / 2;
            else if (alignment == Draw::ALIGN_RIGHT)
                penX = x - tm.line_widths[0];
        }

        for (auto& r : tm.runs)
        {
            if (r.first == nullptr && r.second == "\n")
            {
                line_index++;
                penY += font_set->font_height;

                // Reset X position for the new line based on alignment
                penX = x;
                if (alignment != Draw::ALIGN_LEFT && line_index < tm.line_widths.size())
                {
                    if (alignment == Draw::ALIGN_CENTER)
                        penX = x - tm.line_widths[line_index] / 2;
                    else if (alignment == Draw::ALIGN_RIGHT)
                        penX = x - tm.line_widths[line_index];
                }
                continue;
            }

            XGlyphInfo gi;
            XftTextExtentsUtf8(display, r.first,
                               (const FcChar8*)r.second.c_str(),
                               (int)r.second.size(), &gi);
            XftDrawStringUtf8(back_draw, col, r.first, penX, penY,
                              (const FcChar8*)r.second.c_str(), (int)r.second.size());
            penX += gi.xOff;
        }
    }

    void drawTextRunsOutline(const TextMetrics& tm, int x, int baselineY,
                             const XftColor* fg, const XftColor* outline_color,
                             FontSet* font_set, Draw::TextAlignment alignment, int outline_thickness = 2)
    {
        const int offsets[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};

        int line_index = 0;
        int penX = x;
        int penY = baselineY;

        // Adjust initial X position based on alignment for the first line
        if (alignment != Draw::ALIGN_LEFT && !tm.line_widths.empty())
        {
            if (alignment == Draw::ALIGN_CENTER)
                penX = x - tm.line_widths[0] / 2;
            else if (alignment == Draw::ALIGN_RIGHT)
                penX = x - tm.line_widths[0];
        }

        for (auto& r : tm.runs)
        {
            if (r.first == nullptr && r.second == "\n")
            {
                line_index++;
                penY += font_set->font_height;

                // Reset X position for the new line based on alignment
                penX = x;
                if (alignment != Draw::ALIGN_LEFT && line_index < tm.line_widths.size())
                {
                    if (alignment == Draw::ALIGN_CENTER)
                        penX = x - tm.line_widths[line_index] / 2;
                    else if (alignment == Draw::ALIGN_RIGHT)
                        penX = x - tm.line_widths[line_index];
                }
                continue;
            }

            for (int i = 0; i < 8; ++i)
            {
                int outlineX = penX + offsets[i][0] * outline_thickness;
                int outlineY = penY + offsets[i][1] * outline_thickness;

                XGlyphInfo gi;
                XftTextExtentsUtf8(display, r.first,
                                   (const FcChar8*)r.second.c_str(),
                                   (int)r.second.size(), &gi);
                XftDrawStringUtf8(back_draw, outline_color, r.first, outlineX, outlineY,
                                  (const FcChar8*)r.second.c_str(), (int)r.second.size());
            }

            XGlyphInfo gi;
            XftTextExtentsUtf8(display, r.first,
                               (const FcChar8*)r.second.c_str(),
                               (int)r.second.size(), &gi);
            XftDrawStringUtf8(back_draw, fg, r.first, penX, penY,
                              (const FcChar8*)r.second.c_str(), (int)r.second.size());
            penX += gi.xOff;
        }
    }

    void createOverlayWindow()
    {
        XVisualInfo vinfo;
        if (!XMatchVisualInfo(display, DefaultScreen(display), 32, TrueColor, &vinfo))
        {
            std::cerr << "No 32-bit TrueColor visual available\n";
            return;
        }
        visual = vinfo.visual;

        colormap = XCreateColormap(display, DefaultRootWindow(display), vinfo.visual, AllocNone);

        XSetWindowAttributes attr{};
        attr.background_pixmap = None;
        attr.background_pixel = rgba_to_pixel(0, 0, 0, 0);
        attr.border_pixel = 0;
        attr.win_gravity = NorthWestGravity;
        attr.bit_gravity = ForgetGravity;
        attr.save_under = 1;
        attr.event_mask = BASIC_EVENT_MASK;
        attr.do_not_propagate_mask = NOT_PROPAGATE_MASK;
        attr.override_redirect = 1;
        attr.colormap = colormap;
        attr.backing_store = Always;

        unsigned long mask = CWColormap | CWBorderPixel | CWBackPixel | CWEventMask |
                             CWWinGravity | CWBitGravity | CWSaveUnder | CWDontPropagate |
                             CWOverrideRedirect | CWBackingStore;

        overlay_window = XCreateWindow(display, DefaultRootWindow(display),
                                       pos_x, pos_y, width, height, 0,
                                       vinfo.depth, InputOutput, vinfo.visual, mask, &attr);

        back_buffer = XCreatePixmap(display, overlay_window, width, height, vinfo.depth);

        XShapeCombineMask(display, overlay_window, ShapeInput, 0, 0, None, ShapeSet);
        allowInputPassthrough(overlay_window);
        XMapWindow(display, overlay_window);

        back_draw = XftDrawCreate(display, back_buffer, visual, colormap);
        gc = XCreateGC(display, back_buffer, 0, 0);
    }

    bool checkTargetWindowExists()
    {
        if (!display || !target_window)
            return false;

        // Check if the target window still exists
        XWindowAttributes attr;
        if (!XGetWindowAttributes(display, target_window, &attr))
        {
            return false;
        }
        return true;
    }

    bool initializeOverlayInternal(const char* window_class)
    {
        if (!display)
        {
            display = XOpenDisplay(0);
            if (!display)
            {
                std::cerr << "Failed to open X display" << std::endl;
                return false;
            }
            screen = DefaultScreen(display);
        }

        Window found_window = 0;
        if (!findWindowByClass(DefaultRootWindow(display), 
                               window_class ? std::string(window_class) : std::string(), 
                               found_window))
        {
            std::cout << "Target window with class '" << (window_class ? window_class : "") 
                      << "' not found, will retry..." << std::endl;
            return false;
        }

        target_window = found_window;
        getWindowGeometry(target_window);
        createOverlayWindow();

        // Initialize colors if not already done
        if (!colors_initialized)
        {
            xft_white = createXftColor(1.0, 1.0, 1.0, 1.0);
            xft_black = createXftColor(0.0, 0.0, 0.0, 1.0);
            xft_ltblue = createXftColor(0.0, 1.0, 1.0, 1.0);
            xft_outline = createXftColor(0.0, 0.0, 0.0, 1.0);
            colors_initialized = true;
        }

        overlay_initialized = true;
        std::cout << "Overlay initialized successfully for window class: " << window_class << std::endl;
        return true;
    }

    void cleanupOverlayInternal()
    {
        if (back_draw)
        {
            XftDrawDestroy(back_draw);
            back_draw = nullptr;
        }
        if (back_buffer)
        {
            XFreePixmap(display, back_buffer);
            back_buffer = 0;
        }
        if (gc)
        {
            XFreeGC(display, gc);
            gc = nullptr;
        }

        // Clean up font cache
        for (auto& entry : font_cache)
        {
            if (entry.font_set.primary)
                XftFontClose(display, entry.font_set.primary);
            for (auto* f : entry.font_set.fallbacks)
                if (f)
                    XftFontClose(display, f);
        }
        font_cache.clear();

        if (colors_initialized)
        {
            XftColorFree(display, visual, colormap, &xft_white);
            XftColorFree(display, visual, colormap, &xft_black);
            XftColorFree(display, visual, colormap, &xft_ltblue);
            XftColorFree(display, visual, colormap, &xft_outline);
            colors_initialized = false;
        }

        if (overlay_window)
        {
            XDestroyWindow(display, overlay_window);
            overlay_window = 0;
        }
        if (colormap)
        {
            XFreeColormap(display, colormap);
            colormap = 0;
        }

        target_window = 0;
        overlay_initialized = false;
        
        std::cout << "Overlay cleaned up" << std::endl;
    }

} // namespace

namespace Draw
{
    void drawStringPlain(const std::string& text, int x, int y,
                         double r, double g, double b,
                         const char* font_family, int font_size,
                         TextAlignment alignment)
    {
        if (!overlay_initialized || !back_draw)
            return;

        FontSet* font_set = getFontSet(font_family, font_size);
        TextMetrics tm = computeTextMetrics(text, font_set);
        XftColor color = createXftColor(r, g, b, 1.0);

        int baseline = y + font_set->line_ascent;
        drawTextRuns(tm, x, baseline, &color, font_set, alignment);

        XftColorFree(display, visual, colormap, &color);
    }

    void drawStringOutline(const std::string& text, int x, int y,
                           double r, double g, double b,
                           double outline_r, double outline_g, double outline_b, double outline_a,
                           double outline_width,
                           const char* font_family, int font_size,
                           TextAlignment alignment)
    {
        if (!overlay_initialized || !back_draw)
            return;

        FontSet* font_set = getFontSet(font_family, font_size);
        TextMetrics tm = computeTextMetrics(text, font_set);
        XftColor fg = createXftColor(r, g, b, 1.0);
        XftColor outline = createXftColor(outline_r, outline_g, outline_b, outline_a);

        int baseline = y + font_set->line_ascent;
        drawTextRunsOutline(tm, x, baseline, &fg, &outline, font_set, alignment, (int)std::max(1.0, outline_width));

        XftColorFree(display, visual, colormap, &fg);
        XftColorFree(display, visual, colormap, &outline);
    }

    void drawStringBackground(const std::string& text, int x, int y,
                              double r, double g, double b,
                              double bg_r, double bg_g, double bg_b, double bg_a,
                              int padding,
                              const char* font_family, int font_size,
                              TextAlignment alignment)
    {
        if (!overlay_initialized || !back_draw)
            return;

        FontSet* font_set = getFontSet(font_family, font_size);
        TextMetrics tm = computeTextMetrics(text, font_set);
        XftColor fg = createXftColor(r, g, b, 1.0);

        unsigned long bg_pixel = rgba_to_pixel(
            (unsigned char)(bg_r * 255.0),
            (unsigned char)(bg_g * 255.0),
            (unsigned char)(bg_b * 255.0),
            (unsigned char)(bg_a * 255.0));

        int rect_width = tm.width + 2 * padding;
        int rect_height = tm.height + 2 * padding;

        // Adjust background position based on alignment
        int bg_x = x - padding;
        if (alignment == ALIGN_CENTER)
            bg_x = x - rect_width / 2;
        else if (alignment == ALIGN_RIGHT)
            bg_x = x - rect_width + padding;

        XSetForeground(display, gc, bg_pixel);
        XFillRectangle(display, back_buffer, gc, bg_x, y - padding, rect_width, rect_height);

        int baseline = y + font_set->line_ascent;
        drawTextRuns(tm, x, baseline, &fg, font_set, alignment);

        XftColorFree(display, visual, colormap, &fg);
    }

    void getTextSize(const std::string& text, int* width, int* height,
                     const char* font_family, int font_size)
    {
        if (!overlay_initialized)
            return;

        FontSet* font_set = getFontSet(font_family, font_size);
        TextMetrics tm = computeTextMetrics(text, font_set);
        if (width)
            *width = tm.width;
        if (height)
            *height = tm.height;
    }
} // namespace Draw

namespace Overlay
{
    bool isInitialized()
    {
        return overlay_initialized;
    }

    void cleanup()
    {
        cleanupOverlayInternal();
    }

    bool tryInitialize(const char* window_class)
    {
        if (overlay_initialized)
        {
            // Check if our current target window still exists
            if (!checkTargetWindowExists())
            {
                std::cout << "Target window lost, cleaning up overlay..." << std::endl;
                cleanupOverlayInternal();
            }
            else
            {
                return true; // Already initialized and window exists
            }
        }

        // Try to initialize with the new window class
        if (window_class)
        {
            current_window_class = window_class;
        }

        if (!current_window_class.empty())
        {
            return initializeOverlayInternal(current_window_class.c_str());
        }

        return false;
    }

    bool initialize(const char* window_class)
    {
        current_window_class = window_class ? window_class : "";
        return tryInitialize(window_class);
    }

    void shutdown()
    {
        cleanupOverlayInternal();
        if (display)
        {
            XCloseDisplay(display);
            display = nullptr;
        }
        textMetricsCache.clear();
    }

    void beginFrame()
    {
        if (!overlay_initialized)
            return;

        XSetForeground(display, gc, rgba_to_pixel(0, 0, 0, 0));
        XFillRectangle(display, back_buffer, gc, 0, 0, width, height);
    }

    void endFrame()
    {
        if (!overlay_initialized)
            return;

        XCopyArea(display, back_buffer, overlay_window, gc, 0, 0, width, height, 0, 0);
        XFlush(display);
    }

    void updateWindowPosition()
    {
        if (!overlay_initialized || !target_window)
            return;

        // Check if target window still exists
        if (!checkTargetWindowExists())
        {
            std::cout << "Target window disappeared during update" << std::endl;
            cleanupOverlayInternal();
            return;
        }

        getWindowGeometry(target_window);
        XMoveResizeWindow(display, overlay_window, pos_x, pos_y, width, height);

        static int last_w = 0, last_h = 0;
        if (width != last_w || height != last_h)
        {
            if (back_draw)
            {
                XftDrawDestroy(back_draw);
                back_draw = nullptr;
            }
            if (back_buffer)
            {
                XFreePixmap(display, back_buffer);
                back_buffer = 0;
            }
            if (gc)
            {
                XFreeGC(display, gc);
                gc = nullptr;
            }

            XVisualInfo vinfo;
            XMatchVisualInfo(display, screen, 32, TrueColor, &vinfo);
            back_buffer = XCreatePixmap(display, overlay_window, width, height, vinfo.depth);
            back_draw = XftDrawCreate(display, back_buffer, visual, colormap);
            gc = XCreateGC(display, back_buffer, 0, 0);

            last_w = width;
            last_h = height;
        }
    }

    int getWidth() 
    { 
        if (!overlay_initialized)
            return 0;
        return width; 
    }
    
    int getHeight() 
    { 
        if (!overlay_initialized)
            return 0;
        return height; 
    }
} // namespace Overlay