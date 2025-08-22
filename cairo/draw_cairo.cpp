#include "draw_cairo.h"
#include <pango/pangocairo.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>
#include <cairo/cairo-xlib.h>
#include <iostream>

#define BASIC_EVENT_MASK (StructureNotifyMask | ExposureMask | PropertyChangeMask)
#define NOT_PROPAGATE_MASK (KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask)

namespace {

// Drawing functions internal state
std::string font_family = "Consolas";
int font_size = 20;

void setLayoutFont(PangoLayout *layout) {
    std::string descStr = font_family + " " + std::to_string(font_size);
    PangoFontDescription *desc = pango_font_description_from_string(descStr.c_str());
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
}

// Overlay window internal state
Display *display = nullptr;
Window target_window = 0;
Window overlay_window = 0;
Colormap colormap = 0;

cairo_surface_t *cairo_surface = nullptr;
cairo_surface_t *offscreen_surface = nullptr;
cairo_t *cr = nullptr;

int width = 0;
int height = 0;
int pos_x = 0;
int pos_y = 0;

bool findWindowByClass(Window root, const std::string &target_class, Window &outWin) {
    Window root_return, parent_return;
    Window *children = nullptr;
    unsigned int nchildren = 0;

    if (XQueryTree(display, root, &root_return, &parent_return, &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; i++) {
            XClassHint classHint;
            if (XGetClassHint(display, children[i], &classHint)) {
                bool match = false;
                if (classHint.res_class && std::string(classHint.res_class) == target_class)
                    match = true;
                if (classHint.res_name) XFree(classHint.res_name);
                if (classHint.res_class) XFree(classHint.res_class);

                if (match) {
                    outWin = children[i];
                    if (children) XFree(children);
                    return true;
                }
            }
            if (findWindowByClass(children[i], target_class, outWin)) {
                if (children) XFree(children);
                return true;
            }
        }
        if (children) XFree(children);
    }
    return false;
}

void getWindowGeometry(Window win) {
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

void allowInputPassthrough(Window w) {
    XserverRegion region = XFixesCreateRegion(display, NULL, 0);
    XFixesSetWindowShapeRegion(display, w, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(display, region);
}

void createOverlayWindow() {
    XVisualInfo vinfo;
    XMatchVisualInfo(display, DefaultScreen(display), 32, TrueColor, &vinfo);

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

    cairo_surface = cairo_xlib_surface_create(display, overlay_window, vinfo.visual, width, height);
}

void ensureOffscreenBuffer() {
    static int last_width = 0, last_height = 0;
    if (!offscreen_surface || last_width != width || last_height != height) {
        if (offscreen_surface)
            cairo_surface_destroy(offscreen_surface);
        offscreen_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        last_width = width;
        last_height = height;
    }
}

} // anonymous namespace

namespace DrawingFunctions {

void setFont(const char* family, int size) {
    font_family = family;
    font_size = size;
}

void drawStringPlain(cairo_t* ctx, const std::string &text, int x, int y, 
                    double r, double g, double b, int align) {
    PangoLayout *layout = pango_cairo_create_layout(ctx);
    setLayoutFont(layout);
    pango_layout_set_text(layout, text.c_str(), -1);

    int pw, ph;
    pango_layout_get_size(layout, &pw, &ph);
    double w = pw / (double)PANGO_SCALE;
    double h = ph / (double)PANGO_SCALE;

    double tx = x;
    if (align == ALIGN_CENTER) tx = x - w / 2.0;
    else if (align == ALIGN_RIGHT) tx = x - w;

    cairo_set_source_rgba(ctx, r, g, b, 1.0);
    cairo_move_to(ctx, tx, y - h / 2.0);
    pango_cairo_show_layout(ctx, layout);

    g_object_unref(layout);
}

void drawStringOutline(cairo_t* ctx, const std::string &text, int x, int y,
                      double r, double g, double b, 
                      double outline_r, double outline_g, double outline_b, double outline_a,
                      double outline_width, int align) {
    PangoLayout *layout = pango_cairo_create_layout(ctx);
    setLayoutFont(layout);
    pango_layout_set_text(layout, text.c_str(), -1);

    int pw, ph;
    pango_layout_get_size(layout, &pw, &ph);
    double w = pw / (double)PANGO_SCALE;
    double h = ph / (double)PANGO_SCALE;

    double tx = x;
    if (align == ALIGN_CENTER) tx = x - w / 2.0;
    else if (align == ALIGN_RIGHT) tx = x - w;

    cairo_save(ctx);
    cairo_set_source_rgba(ctx, outline_r, outline_g, outline_b, outline_a);
    cairo_set_line_width(ctx, outline_width);
    cairo_move_to(ctx, tx, y - h / 2.0);
    pango_cairo_layout_path(ctx, layout);
    cairo_stroke(ctx);
    cairo_restore(ctx);

    cairo_set_source_rgba(ctx, r, g, b, 1.0);
    cairo_move_to(ctx, tx, y - h / 2.0);
    pango_cairo_show_layout(ctx, layout);

    g_object_unref(layout);
}

void drawStringBackground(cairo_t* ctx, const std::string &text, int x, int y,
                         double r, double g, double b, 
                         double bg_r, double bg_g, double bg_b, double bg_a,
                         int padding, int align) {
    PangoLayout *layout = pango_cairo_create_layout(ctx);
    setLayoutFont(layout);
    pango_layout_set_text(layout, text.c_str(), -1);

    int pw, ph;
    pango_layout_get_size(layout, &pw, &ph);
    double w = pw / (double)PANGO_SCALE;
    double h = ph / (double)PANGO_SCALE;

    double tx = x;
    if (align == ALIGN_CENTER) tx = x - w / 2.0;
    else if (align == ALIGN_RIGHT) tx = x - w;

    cairo_set_source_rgba(ctx, bg_r, bg_g, bg_b, bg_a);
    cairo_rectangle(ctx, tx - padding, y - h / 2.0 - padding, w + 2 * padding, h + 2 * padding);
    cairo_fill(ctx);

    cairo_set_source_rgba(ctx, r, g, b, 1.0);
    cairo_move_to(ctx, tx, y - h / 2.0);
    pango_cairo_show_layout(ctx, layout);

    g_object_unref(layout);
}

} // namespace DrawingFunctions

namespace OverlayWindow {

bool initialize(const char* window_class) {
    display = XOpenDisplay(0);
    if (!display) {
        std::cerr << "Failed to open X display" << std::endl;
        return false;
    }

    if (!findWindowByClass(DefaultRootWindow(display), window_class, target_window)) {
        std::cerr << "Could not find window with class '" << window_class << "'\n";
        XCloseDisplay(display);
        display = nullptr;
        return false;
    }

    getWindowGeometry(target_window);
    createOverlayWindow();
    return true;
}

void shutdown() {
    if (cr) cairo_destroy(cr);
    if (cairo_surface) cairo_surface_destroy(cairo_surface);
    if (offscreen_surface) cairo_surface_destroy(offscreen_surface);
    
    if (overlay_window) {
        XDestroyWindow(display, overlay_window);
        overlay_window = 0;
    }
    
    if (colormap) {
        XFreeColormap(display, colormap);
        colormap = 0;
    }
    
    if (display) {
        XCloseDisplay(display);
        display = nullptr;
    }
}

void beginFrame() {
    ensureOffscreenBuffer();
    cr = cairo_create(offscreen_surface);
    
    // Clear with transparent background
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}

void endFrame() {
    if (!cr) return;
    
    cairo_destroy(cr);
    cr = nullptr;
    
    // Blit offscreen buffer to window
    cairo_t* window_cr = cairo_create(cairo_surface);
    cairo_set_operator(window_cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(window_cr, offscreen_surface, 0, 0);
    cairo_paint(window_cr);
    cairo_destroy(window_cr);
    
    cairo_surface_flush(cairo_surface);
    XFlush(display);
}

void updateWindowPosition() {
    getWindowGeometry(target_window);
    XMoveResizeWindow(display, overlay_window, pos_x, pos_y, width, height);
    cairo_xlib_surface_set_size(cairo_surface, width, height);
}

int getWidth() {
    return width;
}

int getHeight() {
    return height;
}

cairo_t* getCairoContext() {
    return cr;
}

} // namespace OverlayWindowcan 