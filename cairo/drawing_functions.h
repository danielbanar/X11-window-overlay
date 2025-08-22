#ifndef DRAWING_FUNCTIONS_H
#define DRAWING_FUNCTIONS_H

#include <cairo/cairo.h>
#include <string>

#define ALIGN_CENTER 1
#define ALIGN_LEFT 2
#define ALIGN_RIGHT 3

namespace DrawingFunctions {

// Set the default font for text drawing
void setFont(const char* family, int size);

// Draw text with a solid color
void drawStringPlain(cairo_t* ctx, const std::string &text, int x, int y, 
                     double r, double g, double b, int align);
                     
// Draw text with an outline
void drawStringOutline(cairo_t* ctx, const std::string &text, int x, int y,
                       double r, double g, double b, 
                       double outline_r, double outline_g, double outline_b, double outline_a,
                       double outline_width, int align);
                       
// Draw text with a background rectangle
void drawStringBackground(cairo_t* ctx, const std::string &text, int x, int y,
                          double r, double g, double b, 
                          double bg_r, double bg_g, double bg_b, double bg_a,
                          int padding, int align);

} // namespace DrawingFunctions

#endif