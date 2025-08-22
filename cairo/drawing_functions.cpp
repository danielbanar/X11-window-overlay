#include "drawing_functions.h"
#include <pango/pangocairo.h>
#include <iostream>

namespace {

std::string font_family = "Consolas";
int font_size = 20;

void setLayoutFont(PangoLayout *layout) {
    std::string descStr = font_family + " " + std::to_string(font_size);
    PangoFontDescription *desc = pango_font_description_from_string(descStr.c_str());
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
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