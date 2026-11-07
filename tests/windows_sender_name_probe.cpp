#include <nozzle/discovery.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

int wmain(int argc, wchar_t **argv) {
    if(argc != 2) return 2;
    // The expected name arrives as UTF-8 file bytes, independently of argv conversion.
    std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary);
    if(!input) return 2;
    const std::string expected{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if(expected.empty()) return 2;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while(std::chrono::steady_clock::now() < deadline) {
        for(const auto &sender : nozzle::enumerate_senders()) {
            if(sender.name == expected) {
                std::printf("%s\n", sender.name.c_str());
                return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::fprintf(stderr, "expected output sender was not discovered\n");
    return 1;
}
