#include <chrono>
#include <thread>
#include <iostream>
#include "draw.h"

int main()
{
    if (!Overlay::initialize("GStreamer"))
    {
        return 1;
    }

    Draw::setFont("Consolas", 20);

    auto start_time = std::chrono::steady_clock::now();

    while (true)
    {
        Overlay::updateWindowPosition();
        Overlay::beginFrame();

        int width = Overlay::getWidth();
        int height = Overlay::getHeight();

        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        std::string text = std::to_string(ms) + " ms 🔋↕️🧭🛰️⏱🏠\nHALO";

        int textWidth, textHeight;
        Draw::getTextSize(text, &textWidth, &textHeight);

        // Top-left corner
        Draw::drawStringBackground(text, 0, 0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.6, 6);

        // Top-right corner
        Draw::drawStringOutline(text, width - textWidth, 0, 0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 2.0);

        // Bottom-left corner
        Draw::drawStringBackground(text, 0, height - textHeight, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.6, 6);

        // Bottom-right corner
        Draw::drawStringPlain(text, width - textWidth, height - textHeight, 1.0, 1.0, 1.0);

        Overlay::endFrame();

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    Overlay::shutdown();
    return 0;
}