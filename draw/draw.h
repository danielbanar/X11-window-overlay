#include <string>

namespace Draw
{
    enum TextAlignment
    {
        ALIGN_LEFT,
        ALIGN_CENTER,
        ALIGN_RIGHT
    };

    void drawStringPlain(const std::string& text, int x, int y, double r, double g, double b, const char* font_family = nullptr, int font_size = 0, TextAlignment alignment = ALIGN_LEFT);
    void drawStringOutline(const std::string& text, int x, int y, double r, double g, double b, double outline_r, double outline_g, double outline_b, double outline_a, double outline_width, const char* font_family = nullptr, int font_size = 0, TextAlignment alignment = ALIGN_LEFT);
    void drawStringBackground(const std::string& text, int x, int y, double r, double g, double b, double bg_r, double bg_g, double bg_b, double bg_a, int padding, const char* font_family = nullptr, int font_size = 0, TextAlignment alignment = ALIGN_LEFT);
    void getTextSize(const std::string& text, int* width, int* height, const char* font_family = nullptr, int font_size = 0);
} // namespace Draw

namespace Overlay
{
    bool initialize(const char* window_class);
    void shutdown();
    void beginFrame();
    void endFrame();
    void updateWindowPosition();
    int getWidth();
    int getHeight();
    bool isInitialized();
    void cleanup();
    bool tryInitialize(const char* window_class);
} // namespace Overlay