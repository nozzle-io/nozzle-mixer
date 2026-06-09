#pragma once

#include <cstdint>
#include <string>

namespace nozzle_mixer {

struct smoke_forward_options {
    bool enabled{false};
    bool help{false};
    std::string source_name{};
    std::string output_name{"nozzle-mixer-smoke"};
    std::string evidence_path{};
    uint32_t width{0};
    uint32_t height{0};
    uint64_t min_frames{5};
    uint64_t publish_frames{120};
    uint64_t timeout_ms{120000};
    uint64_t hold_ms{2000};
};

bool has_smoke_forward_request(int argc, char **argv) noexcept;
bool parse_smoke_forward_options(int argc, char **argv, smoke_forward_options &options);
void print_smoke_forward_usage();
int run_smoke_forward(const smoke_forward_options &options);

} // namespace nozzle_mixer
