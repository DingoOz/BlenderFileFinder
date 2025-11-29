#include "app.hpp"
#include "debug.hpp"
#include <iostream>
#include <chrono>
#include <unistd.h>

int main(int argc, char* argv[]) {
    DEBUG_LOG("=== BlenderFileFinder starting ===");
    DEBUG_LOG("PID: " << getpid());

    try {
        DEBUG_LOG("Creating App object...");
        auto appCreateStart = std::chrono::steady_clock::now();
        BlenderFileFinder::App app;
        auto appCreateMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - appCreateStart).count();
        DEBUG_LOG("App object created in " << appCreateMs << "ms");

        DEBUG_LOG("Calling app.init()...");
        auto initStart = std::chrono::steady_clock::now();
        if (!app.init()) {
            std::cerr << "Failed to initialize application\n";
            return 1;
        }
        auto initMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - initStart).count();
        DEBUG_LOG("app.init() completed in " << initMs << "ms");

        DEBUG_LOG("Entering app.run() main loop...");
        app.run();
        DEBUG_LOG("app.run() exited");

        app.shutdown();
        DEBUG_LOG("=== BlenderFileFinder shutdown complete ===");

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        DEBUG_LOG("FATAL EXCEPTION: " << e.what());
        return 1;
    }
}
