#ifndef OVERLAY_WINDOW_H
#define OVERLAY_WINDOW_H

#include <X11/Xlib.h>
#include <cairo/cairo.h>
#include <string>

namespace OverlayWindow {

// Initialize the overlay window system
bool initialize(const char* window_class);

// Clean up resources
void shutdown();

// Begin a new drawing frame
void beginFrame();

// Finish the current drawing frame and display it
void endFrame();

// Update the window position and size to match the target window
void updateWindowPosition();

// Get the current width of the overlay window
int getWidth();

// Get the current height of the overlay window
int getHeight();

// Get the Cairo context for drawing (only valid between beginFrame and endFrame)
cairo_t* getCairoContext();

} // namespace OverlayWindow

#endif