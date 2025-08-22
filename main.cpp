// main.cpp (unchanged)
#include "draw_x11.h"
#include <chrono>
#include <thread>
#include <iostream>

int main() {
    if (!OverlayWindow::initialize("GStreamer")) {
        return 1;
    }
    
    DrawingFunctions::setFont("Consolas", 20);
    
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        OverlayWindow::updateWindowPosition();
        OverlayWindow::beginFrame();
        
        int width = OverlayWindow::getWidth();
        int height = OverlayWindow::getHeight();
        
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        std::string text = std::to_string(ms) + " ms 🔋↕️🧭🛰️⏱🏠";
        
        int padV = 20;
        int padH = 10;
        
        DrawingFunctions::drawStringPlain(text, padH, padV, 1.0, 1.0, 1.0, ALIGN_LEFT);
        DrawingFunctions::drawStringBackground(text, width / 2, padV, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.6, 6, ALIGN_CENTER);
        DrawingFunctions::drawStringOutline(text, width - padH, padV, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 4.0, ALIGN_RIGHT);
        DrawingFunctions::drawStringBackground(text, padH, height - padV, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.6, 6, ALIGN_LEFT);
        DrawingFunctions::drawStringOutline(text, width / 2, height - padV, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 4.0, ALIGN_CENTER);
        DrawingFunctions::drawStringPlain(text, width - padH, height - padV, 1.0, 1.0, 1.0, ALIGN_RIGHT);
        DrawingFunctions::drawStringBackground(text, width / 2, height / 2, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.6, 8, ALIGN_CENTER);
        
        OverlayWindow::endFrame();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    OverlayWindow::shutdown();
    return 0;
}