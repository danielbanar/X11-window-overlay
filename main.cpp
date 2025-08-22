#include <chrono>
#include <thread>
#include <iostream>
#include "draw.h"

int main() {
    if (!Overlay::initialize("GStreamer")) {
        return 1;
    }
    
    Draw::setFont("Consolas", 20);
    
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        Overlay::updateWindowPosition();
        Overlay::beginFrame();
        
        int width = Overlay::getWidth();
        int height = Overlay::getHeight();
        
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        std::string text = std::to_string(ms) + " ms 🔋↕️🧭🛰️⏱🏠";
        
        int padV = 20;
        int padH = 10;
        
        Draw::drawStringPlain(text, padH, padV, 1.0, 1.0, 1.0, ALIGN_LEFT);
        Draw::drawStringBackground(text, width / 2, padV, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.6, 6, ALIGN_CENTER);
        Draw::drawStringOutline(text, width - padH, padV, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 4.0, ALIGN_RIGHT);
        Draw::drawStringBackground(text, padH, height - padV, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.6, 6, ALIGN_LEFT);
        Draw::drawStringOutline(text, width / 2, height - padV, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 4.0, ALIGN_CENTER);
        Draw::drawStringPlain(text, width - padH, height - padV, 1.0, 1.0, 1.0, ALIGN_RIGHT);
        Draw::drawStringBackground(text, width / 2, height / 2, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.6, 8, ALIGN_CENTER);
        
        Overlay::endFrame();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    Overlay::shutdown();
    return 0;
}