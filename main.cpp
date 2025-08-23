#include "draw.h"
#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    if (!Overlay::initialize("GStreamer"))
    {
        return 1;
    }

    auto start_time = std::chrono::steady_clock::now();

    while (true)
    {
        Overlay::updateWindowPosition();
        Overlay::beginFrame();

        int width = Overlay::getWidth();
        int height = Overlay::getHeight();

        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

        // Different texts with different fonts and sizes
        std::string time_text = std::to_string(ms) + " ms";
        std::string emoji_text = "🔋↕️🧭\n🛰️⏱🏠";
        std::string halo_text = "HALO\nJA MILUJEM FICA\nTO TAM ALE MUSITE POVEDAT";

        int textWidth, textHeight;

        // Top-left corner with default font (left aligned)
        Draw::getTextSize(time_text, &textWidth, &textHeight, nullptr, 0);
        Draw::drawStringBackground(time_text, 10, 10, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.6, 6, nullptr, 0, Draw::ALIGN_LEFT);

        // Top-right corner with different font (right aligned)
        Draw::getTextSize(emoji_text, &textWidth, &textHeight, "Arial", 24);
        Draw::drawStringOutline(emoji_text, width - 10, 10, 0, 1.0, 1.0,
                                0.0, 0.0, 0.0, 1.0, 2.0, "Arial", 24, Draw::ALIGN_RIGHT);

        // Middle with another font (centered)
        Draw::getTextSize(halo_text, &textWidth, &textHeight, "Times New Roman", 36);
        Draw::drawStringPlain(halo_text,width/2 - 10, (height - textHeight) / 2,
                              1.0, 0.5, 0.0, "Times New Roman", 36, Draw::ALIGN_RIGHT);

        // Bottom-left corner with another font (left aligned)
        std::string info_text = "FPS: 60";
        Draw::getTextSize(info_text, &textWidth, &textHeight, "Courier New", 18);
        Draw::drawStringBackground(info_text, 10, height - textHeight - 10,
                                   0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.6, 6, "Courier New", 18, Draw::ALIGN_LEFT);

        // Bottom-right corner with default font but different size (right aligned)
        std::string status_text = "Active";
        Draw::getTextSize(status_text, &textWidth, &textHeight, nullptr, 30);
        Draw::drawStringPlain(status_text, width - 10, height - textHeight - 10,
                              0.8, 0.8, 1.0, nullptr, 30, Draw::ALIGN_RIGHT);

        Overlay::endFrame();

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    Overlay::shutdown();
    return 0;
}