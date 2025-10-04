#include "draw.h"
#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    std::string target_window_class = "GStreamer";
    
    // Calculate intervals
    int mainLoopSleepMs = 16; // ~60 FPS
    int windowCheckIntervalMs = 1000; // Check for window every second

    auto start_time = std::chrono::steady_clock::now();
    auto lastWindowCheck = std::chrono::steady_clock::now();

    std::cout << "Starting overlay test application..." << std::endl;
    std::cout << "Target window class: " << target_window_class << std::endl;
    std::cout << "Press Ctrl+C to exit" << std::endl;

    while (true)
    {
        auto loopStart = std::chrono::steady_clock::now();

        // Check for window existence periodically
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastWindowCheck = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastWindowCheck);
        
        if (timeSinceLastWindowCheck.count() >= windowCheckIntervalMs)
        {
            if (Overlay::tryInitialize(target_window_class.c_str()))
            {
                std::cout << "Overlay initialized successfully!" << std::endl;
            }
            else if (!Overlay::isInitialized())
            {
                std::cout << "Waiting for target window..." << std::endl;
            }
            lastWindowCheck = now;
        }

        // Only update and draw if overlay is initialized
        if (Overlay::isInitialized())
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

            // Add overlay status indicator
            std::string overlay_status = "Overlay: INITIALIZED";
            Draw::getTextSize(overlay_status, &textWidth, &textHeight, "Courier New", 14);
            Draw::drawStringBackground(overlay_status, width / 2, height - textHeight - 10,
                                       0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.6, 4, "Courier New", 14, Draw::ALIGN_CENTER);

            Overlay::endFrame();
        }
        else
        {
            // Overlay not initialized - you could display a message on console or do other work
            static int counter = 0;
            if (counter % 100 == 0) { // Only print every 100 iterations to avoid spam
                std::cout << "Overlay not available - waiting for window..." << std::endl;
            }
            counter++;
        }

        // Calculate sleep time to maintain the desired rate
        auto loopEnd = std::chrono::steady_clock::now();
        auto loopTime = std::chrono::duration_cast<std::chrono::milliseconds>(loopEnd - loopStart);
        int sleepTime = mainLoopSleepMs - loopTime.count();

        if (sleepTime > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
        }
    }

    Overlay::shutdown();
    std::cout << "Application exited gracefully" << std::endl;
    return 0;
}