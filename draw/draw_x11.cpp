// draw_cairo.cpp
#include "draw.h"
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <fontconfig/fontconfig.h>
#include <iostream>
#include <vector>
#include <map>

#define BASIC_EVENT_MASK (StructureNotifyMask | ExposureMask | PropertyChangeMask)
#define NOT_PROPAGATE_MASK (KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask)

namespace
{
    Display *display = nullptr;
    int screen = 0;
    Window target_window = 0;
    Window overlay_window = 0;
    Colormap colormap = 0;
    Visual *visual = nullptr;
    Pixmap back_buffer = 0;
    XftDraw *back_draw = nullptr;
    GC gc = nullptr;

    int width = 0;
    int height = 0;
    int pos_x = 0;
    int pos_y = 0;

    struct FontSet
    {
        XftFont *primary = nullptr;
        std::vector<XftFont *> fallbacks;
    } fonts;

    int line_ascent = 0;
    int line_descent = 0;
    int font_height = 0;

    XftColor xft_white, xft_black, xft_ltblue, xft_outline;

    struct TextMetrics
    {
        std::vector<std::pair<XftFont *, std::string>> runs;
        int width;
    };

    std::map<std::string, TextMetrics> textMetricsCache;

    std::string font_family = "Consolas";
    int font_size = 20;
    bool utf8_next(const char *s, int len, int &i, FcChar32 &out)
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
        XftColor color;
        XRenderColor render_color;
        render_color.red = static_cast<short>(r * 0xFFFF);
        render_color.green = static_cast<short>(g * 0xFFFF);
        render_color.blue = static_cast<short>(b * 0xFFFF);
        render_color.alpha = static_cast<short>(a * 0xFFFF);

        if (!XftColorAllocValue(display, visual, colormap, &render_color, &color))
        {
            std::cerr << "Cannot create Xft color" << std::endl;
            exit(1);
        }
        return color;
    }

    void allowInputPassthrough(Window w)
    {
        XserverRegion region = XFixesCreateRegion(display, NULL, 0);
        XFixesSetWindowShapeRegion(display, w, ShapeInput, 0, 0, region);
        XFixesDestroyRegion(display, region);
    }

    bool findWindowByClass(Window root, const std::string &target_class, Window &outWin)
    {
        Window root_return, parent_return;
        Window *children = nullptr;
        unsigned int nchildren = 0;

        if (XQueryTree(display, root, &root_return, &parent_return, &children, &nchildren))
        {
            for (unsigned int i = 0; i < nchildren; i++)
            {
                XClassHint classHint;
                if (XGetClassHint(display, children[i], &classHint))
                {
                    bool match = false;
                    if (classHint.res_class && std::string(classHint.res_class) == target_class)
                        match = true;
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
        XGetWindowAttributes(display, win, &attr);

        Window child;
        int x, y;
        XTranslateCoordinates(display, win, DefaultRootWindow(display), 0, 0, &x, &y, &child);

        pos_x = x;
        pos_y = y;
        width = attr.width;
        height = attr.height;
    }

    XftFont *openFontByFamily(const char *family, double size)
    {
        FcPattern *pat = FcPatternCreate();
        FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)family);
        FcPatternAddDouble(pat, FC_SIZE, size);
        FcConfigSubstitute(nullptr, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);

        FcResult result;
        FcPattern *match = FcFontMatch(nullptr, pat, &result);
        FcPatternDestroy(pat);
        if (!match)
            return nullptr;

        return XftFontOpenPattern(display, match);
    }

    void computeLineMetrics()
    {
        line_ascent = 0;
        line_descent = 0;
        if (fonts.primary)
        {
            line_ascent = std::max(line_ascent, fonts.primary->ascent);
            line_descent = std::max(line_descent, fonts.primary->descent);
        }
        for (auto *f : fonts.fallbacks)
        {
            if (!f)
                continue;
            line_ascent = std::max(line_ascent, f->ascent);
            line_descent = std::max(line_descent, f->descent);
        }
        font_height = line_ascent + line_descent;
    }

    bool loadFonts()
    {
        if (!FcInit())
        {
            std::cerr << "Failed to initialize FontConfig\n";
            return false;
        }

        fonts.primary = openFontByFamily(font_family.c_str(), font_size);
        if (!fonts.primary)
        {
            std::cerr << "Failed to load primary font: " << font_family << "\n";
            return false;
        }

        const char *fallbackFamilies[] = {
            "Noto Color Emoji", "Noto Emoji", "EmojiOne Color", "Twitter Color Emoji",
            "Segoe UI Symbol", "Symbola", "DejaVu Sans", "DejaVu Sans Mono", "Liberation Sans"};

        for (const char *fam : fallbackFamilies)
        {
            if (auto *f = openFontByFamily(fam, font_size))
            {
                fonts.fallbacks.push_back(f);
            }
        }

        computeLineMetrics();
        return true;
    }

    XftFont *pickFontForChar(FcChar32 ch)
    {
        if (fonts.primary && XftCharExists(display, fonts.primary, ch))
            return fonts.primary;

        for (auto *f : fonts.fallbacks)
            if (f && XftCharExists(display, f, ch))
                return f;

        return fonts.primary;
    }

    void utf8ToFontRuns(const char *text, int len, std::vector<std::pair<XftFont *, std::string>> &runs)
    {
        int i = 0;
        XftFont *current = nullptr;
        std::string buf;

        while (i < len)
        {
            FcChar32 cp;
            int before = i;
            utf8_next(text, len, i, cp);
            XftFont *f = pickFontForChar(cp);

            if (current == nullptr)
            {
                current = f;
            }
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

    TextMetrics computeTextMetrics(const std::string &text)
    {
        // Check cache first
        if (textMetricsCache.find(text) != textMetricsCache.end())
        {
            return textMetricsCache[text];
        }

        TextMetrics tm;
        int len = text.length();
        utf8ToFontRuns(text.c_str(), len, tm.runs);

        tm.width = 0;
        for (auto &r : tm.runs)
        {
            XGlyphInfo gi;
            XftTextExtentsUtf8(display, r.first, (const FcChar8 *)r.second.c_str(), r.second.size(), &gi);
            tm.width += gi.xOff;
        }

        // Cache the result
        textMetricsCache[text] = tm;
        return tm;
    }

    void drawTextRuns(const TextMetrics &tm, int x, int baselineY, const XftColor *col)
    {
        int penX = x;
        for (auto &r : tm.runs)
        {
            XGlyphInfo gi;
            XftTextExtentsUtf8(display, r.first, (const FcChar8 *)r.second.c_str(), r.second.size(), &gi);
            XftDrawStringUtf8(back_draw, col, r.first, penX, baselineY,
                              (const FcChar8 *)r.second.c_str(), r.second.size());
            penX += gi.xOff;
        }
    }

    void drawTextRunsOutline(const TextMetrics &tm, int x, int baselineY,
                             const XftColor *fg, const XftColor *outline_color, int outline_thickness)
    {
        // Draw outline first
        const int offsets[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};

        for (int i = 0; i < 8; i++)
        {
            int penX = x + offsets[i][0] * outline_thickness;
            int penY = baselineY + offsets[i][1] * outline_thickness;

            for (auto &r : tm.runs)
            {
                XGlyphInfo gi;
                XftTextExtentsUtf8(display, r.first, (const FcChar8 *)r.second.c_str(), r.second.size(), &gi);
                XftDrawStringUtf8(back_draw, outline_color, r.first, penX, penY,
                                  (const FcChar8 *)r.second.c_str(), r.second.size());
                penX += gi.xOff;
            }
        }

        // Draw main text
        int penX = x;
        for (auto &r : tm.runs)
        {
            XGlyphInfo gi;
            XftTextExtentsUtf8(display, r.first, (const FcChar8 *)r.second.c_str(), r.second.size(), &gi);
            XftDrawStringUtf8(back_draw, fg, r.first, penX, baselineY,
                              (const FcChar8 *)r.second.c_str(), r.second.size());
            penX += gi.xOff;
        }
    }

    void createOverlayWindow()
    {
        XVisualInfo vinfo;
        XMatchVisualInfo(display, DefaultScreen(display), 32, TrueColor, &vinfo);
        visual = vinfo.visual;

        colormap = XCreateColormap(display, DefaultRootWindow(display), vinfo.visual, AllocNone);
        XSetWindowAttributes attr{};
        attr.background_pixmap = None;
        attr.border_pixel = 0;
        attr.override_redirect = 1;
        attr.colormap = colormap;
        attr.event_mask = BASIC_EVENT_MASK;
        attr.do_not_propagate_mask = NOT_PROPAGATE_MASK;

        unsigned long mask = CWColormap | CWBorderPixel | CWEventMask | CWDontPropagate | CWOverrideRedirect;

        overlay_window = XCreateWindow(display, DefaultRootWindow(display), pos_x, pos_y, width, height, 0,
                                       vinfo.depth, InputOutput, vinfo.visual, mask, &attr);

        XShapeCombineMask(display, overlay_window, ShapeInput, 0, 0, None, ShapeSet);
        allowInputPassthrough(overlay_window);
        XMapWindow(display, overlay_window);

        // Create back buffer
        back_buffer = XCreatePixmap(display, overlay_window, width, height, vinfo.depth);
        back_draw = XftDrawCreate(display, back_buffer, visual, colormap);
        gc = XCreateGC(display, back_buffer, 0, 0);
    }

}

namespace Draw
{

    void setFont(const char *family, int size)
    {
        font_family = family;
        font_size = size;

        // Reload fonts with new settings
        if (fonts.primary)
            XftFontClose(display, fonts.primary);
        for (auto *f : fonts.fallbacks)
            if (f)
                XftFontClose(display, f);
        fonts.fallbacks.clear();

        loadFonts();
        textMetricsCache.clear(); // Clear cache when font changes
    }

    void drawStringPlain(const std::string &text, int x, int y,
                         double r, double g, double b, int align)
    {
        TextMetrics tm = computeTextMetrics(text);
        XftColor color = createXftColor(r, g, b);

        int text_x = x;
        if (align == ALIGN_CENTER)
            text_x = x - tm.width / 2;
        else if (align == ALIGN_RIGHT)
            text_x = x - tm.width;

        int baseline = y + line_ascent;
        drawTextRuns(tm, text_x, baseline, &color);
        XftColorFree(display, visual, colormap, &color);
    }

    void drawStringOutline(const std::string &text, int x, int y,
                           double r, double g, double b,
                           double outline_r, double outline_g, double outline_b, double outline_a,
                           double outline_width, int align)
    {
        TextMetrics tm = computeTextMetrics(text);
        XftColor fg_color = createXftColor(r, g, b);
        XftColor outline_color = createXftColor(outline_r, outline_g, outline_b, outline_a);

        int text_x = x;
        if (align == ALIGN_CENTER)
            text_x = x - tm.width / 2;
        else if (align == ALIGN_RIGHT)
            text_x = x - tm.width;

        int baseline = y + line_ascent;
        drawTextRunsOutline(tm, text_x, baseline, &fg_color, &outline_color, static_cast<int>(outline_width));

        XftColorFree(display, visual, colormap, &fg_color);
        XftColorFree(display, visual, colormap, &outline_color);
    }

    void drawStringBackground(const std::string &text, int x, int y,
                              double r, double g, double b,
                              double bg_r, double bg_g, double bg_b, double bg_a,
                              int padding, int align)
    {
        TextMetrics tm = computeTextMetrics(text);
        XftColor fg_color = createXftColor(r, g, b);
        XftColor bg_color = createXftColor(bg_r, bg_g, bg_b, bg_a);

        int rect_width = tm.width + 2 * padding;
        int rect_height = font_height + 2 * padding;

        int rect_x = x;
        int text_x = x;

        switch (align)
        {
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

        XSetForeground(display, gc, bg_color.pixel);
        XFillRectangle(display, back_buffer, gc, rect_x, y - padding, rect_width, rect_height);

        int baseline = y + line_ascent;
        drawTextRuns(tm, text_x, baseline, &fg_color);

        XftColorFree(display, visual, colormap, &fg_color);
        XftColorFree(display, visual, colormap, &bg_color);
    }

}

namespace Overlay
{

    bool initialize(const char *window_class)
    {
        display = XOpenDisplay(0);
        if (!display)
        {
            std::cerr << "Failed to open X display" << std::endl;
            return false;
        }

        screen = DefaultScreen(display);

        if (!findWindowByClass(DefaultRootWindow(display), window_class, target_window))
        {
            std::cerr << "Could not find window with class '" << window_class << "'\n";
            XCloseDisplay(display);
            display = nullptr;
            return false;
        }

        getWindowGeometry(target_window);
        createOverlayWindow();

        if (!loadFonts())
        {
            std::cerr << "Failed to load fonts\n";
            return false;
        }

        xft_white = createXftColor(1.0, 1.0, 1.0);
        xft_black = createXftColor(0.0, 0.0, 0.0);
        xft_ltblue = createXftColor(0.0, 1.0, 1.0);
        xft_outline = createXftColor(0.0, 0.0, 0.0, 1.0);

        return true;
    }

    void shutdown()
    {
        if (back_draw)
            XftDrawDestroy(back_draw);
        if (back_buffer)
            XFreePixmap(display, back_buffer);
        if (gc)
            XFreeGC(display, gc);

        if (fonts.primary)
            XftFontClose(display, fonts.primary);
        for (auto *f : fonts.fallbacks)
            if (f)
                XftFontClose(display, f);
        fonts.fallbacks.clear();

        XftColorFree(display, visual, colormap, &xft_white);
        XftColorFree(display, visual, colormap, &xft_black);
        XftColorFree(display, visual, colormap, &xft_ltblue);
        XftColorFree(display, visual, colormap, &xft_outline);

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

        if (display)
        {
            XCloseDisplay(display);
            display = nullptr;
        }

        textMetricsCache.clear();
    }

    void beginFrame()
    {
        // Clear back buffer with transparent background
        XSetForeground(display, gc, 0);
        XFillRectangle(display, back_buffer, gc, 0, 0, width, height);
    }

    void endFrame()
    {
        // Copy back buffer to window
        XCopyArea(display, back_buffer, overlay_window, gc, 0, 0, width, height, 0, 0);
        XFlush(display);
    }

    void updateWindowPosition()
    {
        getWindowGeometry(target_window);
        XMoveResizeWindow(display, overlay_window, pos_x, pos_y, width, height);

        // Recreate back buffer if size changed
        static int last_width = 0, last_height = 0;
        if (width != last_width || height != last_height)
        {
            if (back_draw)
                XftDrawDestroy(back_draw);
            if (back_buffer)
                XFreePixmap(display, back_buffer);
            if (gc)
                XFreeGC(display, gc);

            XVisualInfo vinfo;
            XMatchVisualInfo(display, screen, 32, TrueColor, &vinfo);
            back_buffer = XCreatePixmap(display, overlay_window, width, height, vinfo.depth);
            back_draw = XftDrawCreate(display, back_buffer, visual, colormap);
            gc = XCreateGC(display, back_buffer, 0, 0);

            last_width = width;
            last_height = height;
        }
    }

    int getWidth()
    {
        return width;
    }

    int getHeight()
    {
        return height;
    }
}