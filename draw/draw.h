#include <string>

namespace Draw
{
    void drawStringPlain(const std::string &text, int x, int y, double r, double g, double b, const char* font_family = nullptr, int font_size = 0);
    void drawStringOutline(const std::string &text, int x, int y, double r, double g, double b, double outline_r, double outline_g, double outline_b, double outline_a, double outline_width, const char* font_family = nullptr, int font_size = 0);
    void drawStringBackground(const std::string &text, int x, int y, double r, double g, double b, double bg_r, double bg_g, double bg_b, double bg_a, int padding, const char* font_family = nullptr, int font_size = 0);
    void getTextSize(const std::string &text, int *width, int *height, const char* font_family = nullptr, int font_size = 0);
}

namespace Overlay
{
    bool initialize(const char *window_class);
    void shutdown();
    void beginFrame();
    void endFrame();
    void updateWindowPosition();
    int getWidth();
    int getHeight();
}