#include "draw.h"
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <cairo/cairo-xlib.h>
#include <iostream>
#include <pango/pangocairo.h>

#define BASIC_EVENT_MASK (StructureNotifyMask | ExposureMask | PropertyChangeMask)
#define NOT_PROPAGATE_MASK (KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask)

namespace
{
    cairo_t* current_cr = nullptr;

    Display* display = nullptr;
    Window target_window = 0;
    Window overlay_window = 0;
    Colormap colormap = 0;

    cairo_surface_t* cairo_surface = nullptr;
    cairo_surface_t* offscreen_surface = nullptr;
    cairo_t* cr = nullptr;

    int width = 0;
    int height = 0;
    int pos_x = 0;
    int pos_y = 0;

    bool overlay_initialized = false;
    std::string current_window_class;
    bool x_error_occurred = false;

    // X error handler
    int xErrorHandler(Display* dpy, XErrorEvent* event)
    {
        char error_text[256];
        XGetErrorText(dpy, event->error_code, error_text, sizeof(error_text));
        std::cerr << "X Error: " << error_text 
                  << " (Major: " << (int)event->request_code
                  << ", Minor: " << (int)event->minor_code
                  << ", Resource: 0x" << std::hex << event->resourceid << std::dec << ")" << std::endl;
        
        x_error_occurred = true;
        return 0;
    }

    void setLayoutFont(PangoLayout* layout, const char* font_family, int font_size)
    {
        // Use default values if not specified
        const char* family = font_family ? font_family : "Consolas";
        int size = font_size > 0 ? font_size : 20;

        std::string descStr = std::string(family) + " " + std::to_string(size);
        PangoFontDescription* desc = pango_font_description_from_string(descStr.c_str());
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
    }

    bool findWindowByClass(Window root, const std::string& target_class, Window& outWin)
    {
        Window root_return, parent_return;
        Window* children = nullptr;
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

    bool getWindowGeometry(Window win)
    {
        if (!display || !win)
            return false;

        // Reset error flag
        x_error_occurred = false;
        
        XWindowAttributes attr;
        if (!XGetWindowAttributes(display, win, &attr))
        {
            if (!x_error_occurred) {
                std::cerr << "Failed to get window attributes, window may have closed\n";
            }
            return false;
        }

        // Check if an error occurred during XGetWindowAttributes
        if (x_error_occurred) {
            return false;
        }

        Window child;
        int x, y;
        if (!XTranslateCoordinates(display, win, DefaultRootWindow(display), 0, 0, &x, &y, &child))
        {
            if (!x_error_occurred) {
                std::cerr << "Failed to translate window coordinates\n";
            }
            return false;
        }

        // Check if an error occurred during XTranslateCoordinates
        if (x_error_occurred) {
            return false;
        }

        pos_x = x;
        pos_y = y;
        width = attr.width;
        height = attr.height;
        
        return true;
    }

    void allowInputPassthrough(Window w)
    {
        XserverRegion region = XFixesCreateRegion(display, NULL, 0);
        XFixesSetWindowShapeRegion(display, w, ShapeInput, 0, 0, region);
        XFixesDestroyRegion(display, region);
    }

    void createOverlayWindow()
    {
        XVisualInfo vinfo;
        if (!XMatchVisualInfo(display, DefaultScreen(display), 32, TrueColor, &vinfo))
        {
            std::cerr << "No 32-bit TrueColor visual available\n";
            return;
        }

        colormap = XCreateColormap(display, DefaultRootWindow(display), vinfo.visual, AllocNone);
        XSetWindowAttributes attr{};
        attr.background_pixmap = None;
        attr.border_pixel = 0;
        attr.override_redirect = 1;
        attr.colormap = colormap;
        attr.event_mask = BASIC_EVENT_MASK;
        attr.do_not_propagate_mask = NOT_PROPAGATE_MASK;

        unsigned long mask = CWColormap | CWBorderPixel | CWEventMask | CWDontPropagate | CWOverrideRedirect;

        overlay_window = XCreateWindow(display, DefaultRootWindow(display), pos_x, pos_y, width, height, 0, vinfo.depth,
                                       InputOutput, vinfo.visual, mask, &attr);

        XShapeCombineMask(display, overlay_window, ShapeInput, 0, 0, None, ShapeSet);
        allowInputPassthrough(overlay_window);
        XMapWindow(display, overlay_window);

        cairo_surface = cairo_xlib_surface_create(display, overlay_window, vinfo.visual, width, height);
    }

    void ensureOffscreenBuffer()
    {
        static int last_width = 0, last_height = 0;
        if (!offscreen_surface || last_width != width || last_height != height)
        {
            if (offscreen_surface)
                cairo_surface_destroy(offscreen_surface);
            offscreen_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
            last_width = width;
            last_height = height;
        }
    }

    bool checkTargetWindowExists()
    {
        if (!display || !target_window)
            return false;

        // Reset error flag
        x_error_occurred = false;
        
        // Check if the target window still exists
        XWindowAttributes attr;
        if (!XGetWindowAttributes(display, target_window, &attr))
        {
            // If we get here and x_error_occurred is true, it means we got a BadWindow error
            return false;
        }
        
        // Check if an X error occurred
        if (x_error_occurred) {
            return false;
        }
        
        return true;
    }

    bool initializeOverlayInternal(const char* window_class)
    {
        if (!display)
        {
            // Set up X error handler before opening display
            XSetErrorHandler(xErrorHandler);
            
            display = XOpenDisplay(0);
            if (!display)
            {
                std::cerr << "Failed to open X display" << std::endl;
                return false;
            }
        }

        Window found_window = 0;
        if (!findWindowByClass(DefaultRootWindow(display), window_class, found_window))
        {
            std::cout << "Target window with class '" << window_class << "' not found, will retry..." << std::endl;
            return false;
        }

        target_window = found_window;
        if (!getWindowGeometry(target_window)) {
            std::cerr << "Failed to get window geometry" << std::endl;
            return false;
        }
        createOverlayWindow();

        overlay_initialized = true;
        std::cout << "Overlay initialized successfully for window class: " << window_class << std::endl;
        return true;
    }

    void cleanupOverlayInternal()
    {
        if (cr)
        {
            cairo_destroy(cr);
            cr = nullptr;
        }
        if (cairo_surface)
        {
            cairo_surface_destroy(cairo_surface);
            cairo_surface = nullptr;
        }
        if (offscreen_surface)
        {
            cairo_surface_destroy(offscreen_surface);
            offscreen_surface = nullptr;
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
        current_cr = nullptr;
        
        std::cout << "Overlay cleaned up" << std::endl;
    }

} // namespace

namespace Draw
{
    void drawStringPlain(const std::string& text, int x, int y, double r, double g, double b, const char* font_family,
                         int font_size, TextAlignment alignment)
    {
        if (!current_cr)
            return;

        PangoLayout* layout = pango_cairo_create_layout(current_cr);
        setLayoutFont(layout, font_family, font_size);
        pango_layout_set_text(layout, text.c_str(), -1);
        
        // Set alignment
        PangoAlignment pango_align = PANGO_ALIGN_LEFT;
        if (alignment == ALIGN_CENTER)
            pango_align = PANGO_ALIGN_CENTER;
        else if (alignment == ALIGN_RIGHT)
            pango_align = PANGO_ALIGN_RIGHT;
        
        pango_layout_set_alignment(layout, pango_align);

        // Get text extents to adjust position
        int text_width, text_height;
        pango_layout_get_pixel_size(layout, &text_width, &text_height);
        
        // Adjust x position based on alignment
        int draw_x = x;
        if (alignment == ALIGN_CENTER)
            draw_x = x - text_width / 2;
        else if (alignment == ALIGN_RIGHT)
            draw_x = x - text_width;

        cairo_set_source_rgba(current_cr, r, g, b, 1.0);
        cairo_move_to(current_cr, draw_x, y);
        pango_cairo_show_layout(current_cr, layout);

        g_object_unref(layout);
    }

    void drawStringOutline(const std::string& text, int x, int y, double r, double g, double b, double outline_r,
                           double outline_g, double outline_b, double outline_a, double outline_width,
                           const char* font_family, int font_size, TextAlignment alignment)
    {
        if (!current_cr)
            return;

        PangoLayout* layout = pango_cairo_create_layout(current_cr);
        setLayoutFont(layout, font_family, font_size);
        pango_layout_set_text(layout, text.c_str(), -1);
        
        // Set alignment
        PangoAlignment pango_align = PANGO_ALIGN_LEFT;
        if (alignment == ALIGN_CENTER)
            pango_align = PANGO_ALIGN_CENTER;
        else if (alignment == ALIGN_RIGHT)
            pango_align = PANGO_ALIGN_RIGHT;
        
        pango_layout_set_alignment(layout, pango_align);

        // Get text extents to adjust position
        int text_width, text_height;
        pango_layout_get_pixel_size(layout, &text_width, &text_height);
        
        // Adjust x position based on alignment
        int draw_x = x;
        if (alignment == ALIGN_CENTER)
            draw_x = x - text_width / 2;
        else if (alignment == ALIGN_RIGHT)
            draw_x = x - text_width;

        cairo_save(current_cr);
        cairo_set_source_rgba(current_cr, outline_r, outline_g, outline_b, outline_a);
        cairo_set_line_width(current_cr, outline_width * 2);
        cairo_move_to(current_cr, draw_x, y);
        pango_cairo_layout_path(current_cr, layout);
        cairo_stroke(current_cr);
        cairo_restore(current_cr);

        cairo_set_source_rgba(current_cr, r, g, b, 1.0);
        cairo_move_to(current_cr, draw_x, y);
        pango_cairo_show_layout(current_cr, layout);

        g_object_unref(layout);
    }

    void drawStringBackground(const std::string& text, int x, int y, double r, double g, double b, double bg_r,
                              double bg_g, double bg_b, double bg_a, int padding, const char* font_family,
                              int font_size, TextAlignment alignment)
    {
        if (!current_cr)
            return;

        PangoLayout* layout = pango_cairo_create_layout(current_cr);
        setLayoutFont(layout, font_family, font_size);
        pango_layout_set_text(layout, text.c_str(), -1);
        
        // Set alignment
        PangoAlignment pango_align = PANGO_ALIGN_LEFT;
        if (alignment == ALIGN_CENTER)
            pango_align = PANGO_ALIGN_CENTER;
        else if (alignment == ALIGN_RIGHT)
            pango_align = PANGO_ALIGN_RIGHT;
        
        pango_layout_set_alignment(layout, pango_align);

        // Get text extents to adjust position
        int text_width, text_height;
        pango_layout_get_pixel_size(layout, &text_width, &text_height);
        
        // Adjust x position based on alignment
        int draw_x = x;
        if (alignment == ALIGN_CENTER)
            draw_x = x - text_width / 2;
        else if (alignment == ALIGN_RIGHT)
            draw_x = x - text_width;

        // Adjust background position based on alignment
        int bg_x = draw_x - padding;
        int bg_y = y - padding;
        int bg_width = text_width + 2 * padding;
        int bg_height = text_height + 2 * padding;

        cairo_set_source_rgba(current_cr, bg_r, bg_g, bg_b, bg_a);
        cairo_rectangle(current_cr, bg_x, bg_y, bg_width, bg_height);
        cairo_fill(current_cr);

        cairo_set_source_rgba(current_cr, r, g, b, 1.0);
        cairo_move_to(current_cr, draw_x, y);
        pango_cairo_show_layout(current_cr, layout);

        g_object_unref(layout);
    }

    void getTextSize(const std::string& text, int* width, int* height, const char* font_family, int font_size)
    {
        if (!current_cr)
            return;

        PangoLayout* layout = pango_cairo_create_layout(current_cr);
        setLayoutFont(layout, font_family, font_size);
        pango_layout_set_text(layout, text.c_str(), -1);

        int w, h;
        pango_layout_get_pixel_size(layout, &w, &h);

        if (width)
            *width = w;
        if (height)
            *height = h;

        g_object_unref(layout);
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
    }

    void beginFrame()
    {
        if (!overlay_initialized)
            return;

        ensureOffscreenBuffer();
        cr = cairo_create(offscreen_surface);
        current_cr = cr;

        // Clear with transparent background
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    }

    void endFrame()
    {
        if (!cr || !overlay_initialized)
            return;

        cairo_destroy(cr);
        cr = nullptr;
        current_cr = nullptr;

        // Blit offscreen buffer to window
        cairo_t* window_cr = cairo_create(cairo_surface);
        cairo_set_operator(window_cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_surface(window_cr, offscreen_surface, 0, 0);
        cairo_paint(window_cr);
        cairo_destroy(window_cr);

        cairo_surface_flush(cairo_surface);
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

        // Use the safe version of getWindowGeometry
        if (!getWindowGeometry(target_window)) {
            std::cout << "Failed to get window geometry, cleaning up overlay" << std::endl;
            cleanupOverlayInternal();
            return;
        }

        XMoveResizeWindow(display, overlay_window, pos_x, pos_y, width, height);
        cairo_xlib_surface_set_size(cairo_surface, width, height);
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