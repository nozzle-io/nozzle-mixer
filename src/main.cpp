#include <app/smoke_forward.hpp>
#include <gui/gui.hpp>

#include <cstdio>

int main(int argc, char **argv) {
    if(nozzle_mixer::has_smoke_forward_request(argc, argv)) {
        nozzle_mixer::smoke_forward_options smoke_options{};
        if(!nozzle_mixer::parse_smoke_forward_options(argc, argv, smoke_options)) {
            return 2;
        }
        if(smoke_options.help) {
            nozzle_mixer::print_smoke_forward_usage();
            return 0;
        }
        if(smoke_options.enabled) {
            return nozzle_mixer::run_smoke_forward(smoke_options);
        }
    }

    nozzle_mixer::gui app;
    if(!app.init()) {
        std::fprintf(stderr, "failed to initialize nozzle-mixer\n");
        return 1;
    }
    app.run();
    return 0;
}
