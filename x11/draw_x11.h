// draw_cairo.h
#ifndef DRAW_CAIRO_H
#define DRAW_CAIRO_H

#include <X11/Xlib.h>
#include <string>

#define ALIGN_CENTER 1
#define ALIGN_LEFT 2
#define ALIGN_RIGHT 3

namespace DrawingFunctions {

// Set the default font for text drawing
void setFont(const char* family, int size);

// Draw text with a solid color
void drawStringPlain(const std::string &text, int x, int y, 
                     double r, double g, double b, int align);
                     
// Draw text with an outline
void drawStringOutline(const std::string &text, int x, int y,
                       double r, double g, double b, 
                       double outline_r, double outline_g, double outline_b, double outline_a,
                       double outline_width, int align);
                       
// Draw text with a background rectangle
void drawStringBackground(const std::string &text, int x, int y,
                          double r, double g, double b, 
                          double bg_r, double bg_g, double bg_b, double bg_a,
                          int padding, int align);

} // namespace DrawingFunctions

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

} // namespace OverlayWindow

#endif