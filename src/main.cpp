#include <gui/gui.hpp>

#include <cstdio>

int main() {
    nozzle_mixer::gui app;
    if (!app.init()) {
        std::fprintf(stderr, "failed to initialize nozzle-mixer\n");
        return 1;
    }
    app.run();
    return 0;
}
