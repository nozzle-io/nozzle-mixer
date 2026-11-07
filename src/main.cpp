#include "windows_utf8_argv.hpp"

#include <app/smoke_forward.hpp>
#include <gui/gui.hpp>

#include <cstdio>

#if defined(_WIN32)
int wmain(int argc, wchar_t **wide_argv) {
    windows_utf8_argv converted;
    if(!converted.assign(argc, wide_argv)) {
        std::fprintf(stderr, "Invalid UTF-16 command line (Win32 error %lu)\n",
            static_cast<unsigned long>(GetLastError()));
        return 2;
    }
    char **argv = converted.data();
#else
int main(int argc, char **argv) {
#endif
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
