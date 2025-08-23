#include <string>

namespace Draw
{
    void setFont(const char *family, int size);
    void drawStringPlain(const std::string &text, int x, int y, double r, double g, double b);
    void drawStringOutline(const std::string &text, int x, int y, double r, double g, double b, double outline_r, double outline_g, double outline_b, double outline_a, double outline_width);
    void drawStringBackground(const std::string &text, int x, int y, double r, double g, double b, double bg_r, double bg_g, double bg_b, double bg_a, int padding);
    void getTextSize(const std::string &text, int *width, int *height);
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