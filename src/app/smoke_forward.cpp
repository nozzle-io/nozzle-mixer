#include <app/smoke_forward.hpp>

#include <app/output_publisher.hpp>
#include <gpu_compositor.hpp>
#include <app/source_registry.hpp>

#include <nozzle/receiver.hpp>

#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace nozzle_mixer {

namespace {

struct smoke_forward_result {
    bool passed{false};
    std::string failure_reason{};
    std::string create_error{};
    std::string acquire_error{};
    std::string compositor_error{};
    std::string publisher_error{};
    std::string compositor_backend{};
    uint32_t observed_width{0};
    uint32_t observed_height{0};
    uint64_t observed_count{0};
    uint64_t distinct_count{0};
    uint64_t published_count{0};
    uint64_t last_frame_index{0};
    std::vector<uint64_t> observed_frame_indices{};
    nozzle::connected_sender_info sender_info{};
};

bool parse_uint64(const char *text, uint64_t &value) {
    if(!text || text[0] == '\0') return false;
    for(const char *cursor = text; *cursor != '\0'; cursor = cursor + 1) {
        if(!std::isdigit((unsigned char)*cursor)) return false;
    }
    errno = 0;
    char *end = nullptr;
    unsigned long long parsed = std::strtoull(text, &end, 10);
    if(errno == ERANGE) return false;
    if(!end || *end != '\0') return false;
    value = (uint64_t)parsed;
    return true;
}

bool parse_uint32(const char *text, uint32_t &value) {
    uint64_t parsed{0};
    if(!parse_uint64(text, parsed)) return false;
    if(0xffffffffull < parsed) return false;
    value = (uint32_t)parsed;
    return true;
}

bool require_value(int argc, char **argv, int &index, const char *name, const char *&value) {
    if(index + 1 >= argc) {
        std::fprintf(stderr, "%s requires a value\n", name);
        return false;
    }
    index = index + 1;
    value = argv[index];
    return true;
}

std::string json_escape(const std::string &value) {
    std::ostringstream stream;
    for(char c : value) {
        switch(c) {
            case '\\': stream << "\\\\"; break;
            case '"': stream << "\\\""; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:
                if(0 <= c && c < 0x20) {
                    stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c << std::dec << std::setfill(' ');
                } else {
                    stream << c;
                }
                break;
        }
    }
    return stream.str();
}


const char *backend_json_name(nozzle::backend_type backend) noexcept {
    switch(backend) {
        case nozzle::backend_type::d3d11: return "D3D11";
        case nozzle::backend_type::metal: return "Metal";
        case nozzle::backend_type::opengl: return "OpenGL";
        case nozzle::backend_type::dma_buf: return "DMA-BUF";
        case nozzle::backend_type::unknown: return "unknown";
    }
    return "unknown";
}

std::string make_evidence_json(const smoke_forward_options &options, const smoke_forward_result &result) {
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"role\": \"mixer_forwarder\",\n";
    stream << "  \"verdict\": \"" << (result.passed ? "PASS" : "FAIL") << "\",\n";
    stream << "  \"failure_reason\": \"" << json_escape(result.failure_reason) << "\",\n";
    stream << "  \"input\": {\n";
    stream << "    \"source_name\": \"" << json_escape(options.source_name) << "\",\n";
    stream << "    \"sender_name\": \"" << json_escape(result.sender_info.name) << "\",\n";
    stream << "    \"sender_application\": \"" << json_escape(result.sender_info.application_name) << "\",\n";
    stream << "    \"backend\": \"" << backend_json_name(result.sender_info.backend) << "\",\n";
    stream << "    \"expected_width\": " << options.width << ",\n";
    stream << "    \"expected_height\": " << options.height << ",\n";
    stream << "    \"observed_width\": " << result.observed_width << ",\n";
    stream << "    \"observed_height\": " << result.observed_height << "\n";
    stream << "  },\n";
    stream << "  \"output\": {\n";
    stream << "    \"name\": \"" << json_escape(options.output_name) << "\",\n";
    stream << "    \"width\": " << options.width << ",\n";
    stream << "    \"height\": " << options.height << ",\n";
    stream << "    \"published_count\": " << result.published_count << "\n";
    stream << "  },\n";
    stream << "  \"gpu_path\": {\n";
    stream << "    \"compositor_backend\": \"" << json_escape(result.compositor_backend) << "\",\n";
    stream << "    \"mode\": \"Cut A\",\n";
    stream << "    \"cpu_readback_used_by_mixer\": false,\n";
    stream << "    \"publish_method\": \"gpu_compositor_publish\"\n";
    stream << "  },\n";
    stream << "  \"frame\": {\n";
    stream << "    \"observed_count\": " << result.observed_count << ",\n";
    stream << "    \"distinct_count\": " << result.distinct_count << ",\n";
    stream << "    \"minimum_required_count\": " << options.min_frames << ",\n";
    stream << "    \"last_frame_index\": " << result.last_frame_index << ",\n";
    stream << "    \"observed_indices\": [";
    for(std::size_t index = 0; index < result.observed_frame_indices.size(); index = index + 1u) {
        if(0 < index) stream << ", ";
        stream << result.observed_frame_indices[index];
    }
    stream << "]\n";
    stream << "  },\n";
    stream << "  \"checks\": {\n";
    stream << "    \"dimensions\": \"" << ((result.observed_width == options.width && result.observed_height == options.height) ? "PASS" : "FAIL") << "\",\n";
    stream << "    \"minimum_distinct_frames\": \"" << (options.min_frames <= result.distinct_count ? "PASS" : "FAIL") << "\",\n";
    stream << "    \"published_frames\": \"" << (0 < result.published_count ? "PASS" : "FAIL") << "\"\n";
    stream << "  },\n";
    stream << "  \"errors\": {\n";
    stream << "    \"create\": \"" << json_escape(result.create_error) << "\",\n";
    stream << "    \"acquire\": \"" << json_escape(result.acquire_error) << "\",\n";
    stream << "    \"compositor\": \"" << json_escape(result.compositor_error) << "\",\n";
    stream << "    \"publisher\": \"" << json_escape(result.publisher_error) << "\"\n";
    stream << "  }\n";
    stream << "}\n";
    return stream.str();
}

bool write_evidence(const std::string &path, const std::string &json) {
    if(path.empty()) return true;
    std::ofstream stream(path);
    if(!stream) return false;
    stream << json;
    return static_cast<bool>(stream);
}

bool validate_options(const smoke_forward_options &options, std::string &error) {
    if(options.source_name.empty()) {
        error = "missing --source";
        return false;
    }
    if(options.output_name.empty()) {
        error = "missing --output";
        return false;
    }
    if(options.width == 0 || options.height == 0) {
        error = "width and height must be non-zero";
        return false;
    }
    if(options.min_frames == 0) {
        error = "min-frames must be non-zero";
        return false;
    }
    if(options.publish_frames < options.min_frames) {
        error = "publish-frames must be >= min-frames";
        return false;
    }
    if(options.timeout_ms == 0) {
        error = "timeout-ms must be non-zero";
        return false;
    }
    return true;
}

} // namespace

bool has_smoke_forward_request(int argc, char **argv) noexcept {
    for(int index = 1; index < argc; index = index + 1) {
        const std::string arg{argv[index]};
        if(arg == "--smoke-forward" || arg == "--help") return true;
    }
    return false;
}

void print_smoke_forward_usage() {
    std::printf("Usage: nozzle-mixer [--smoke-forward --source NAME --output NAME --width N --height N --min-frames N --publish-frames N --timeout-ms N --hold-ms N --evidence PATH]\n");
}

bool parse_smoke_forward_options(int argc, char **argv, smoke_forward_options &options) {
    for(int index = 1; index < argc; index = index + 1) {
        const std::string arg{argv[index]};
        const char *value = nullptr;
        if(arg == "--smoke-forward") {
            options.enabled = true;
        } else if(arg == "--help") {
            options.help = true;
        } else if(arg == "--source") {
            if(!require_value(argc, argv, index, "--source", value)) return false;
            options.source_name = value;
        } else if(arg == "--output") {
            if(!require_value(argc, argv, index, "--output", value)) return false;
            options.output_name = value;
        } else if(arg == "--width") {
            if(!require_value(argc, argv, index, "--width", value) || !parse_uint32(value, options.width)) {
                std::fprintf(stderr, "invalid --width\n");
                return false;
            }
        } else if(arg == "--height") {
            if(!require_value(argc, argv, index, "--height", value) || !parse_uint32(value, options.height)) {
                std::fprintf(stderr, "invalid --height\n");
                return false;
            }
        } else if(arg == "--min-frames") {
            if(!require_value(argc, argv, index, "--min-frames", value) || !parse_uint64(value, options.min_frames)) {
                std::fprintf(stderr, "invalid --min-frames\n");
                return false;
            }
        } else if(arg == "--publish-frames") {
            if(!require_value(argc, argv, index, "--publish-frames", value) || !parse_uint64(value, options.publish_frames)) {
                std::fprintf(stderr, "invalid --publish-frames\n");
                return false;
            }
        } else if(arg == "--timeout-ms") {
            if(!require_value(argc, argv, index, "--timeout-ms", value) || !parse_uint64(value, options.timeout_ms)) {
                std::fprintf(stderr, "invalid --timeout-ms\n");
                return false;
            }
        } else if(arg == "--hold-ms") {
            if(!require_value(argc, argv, index, "--hold-ms", value) || !parse_uint64(value, options.hold_ms)) {
                std::fprintf(stderr, "invalid --hold-ms\n");
                return false;
            }
        } else if(arg == "--evidence") {
            if(!require_value(argc, argv, index, "--evidence", value)) return false;
            options.evidence_path = value;
        } else if(arg.rfind("--", 0) == 0) {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return false;
        }
    }
    return true;
}

int run_smoke_forward(const smoke_forward_options &options) {
    smoke_forward_result result{};
    std::string validation_error{};
    if(!validate_options(options, validation_error)) {
        result.failure_reason = validation_error;
        if(!write_evidence(options.evidence_path, make_evidence_json(options, result))) return 1;
        std::fprintf(stderr, "nozzle-mixer smoke forward FAIL: %s\n", validation_error.c_str());
        return 2;
    }

    std::unique_ptr<gpu_compositor> compositor = create_gpu_compositor();
    if(!compositor || !compositor->init(options.width, options.height)) {
        result.failure_reason = "compositor_init_failed";
        result.compositor_error = compositor ? compositor->last_error() : "create_gpu_compositor returned null";
        if(!write_evidence(options.evidence_path, make_evidence_json(options, result))) return 1;
        std::fprintf(stderr, "nozzle-mixer smoke forward FAIL: %s: %s\n", result.failure_reason.c_str(), result.compositor_error.c_str());
        return 1;
    }
    result.compositor_backend = compositor->backend_name();

    output_publisher publisher{};
    if(!publisher.start(options.output_name, options.width, options.height)) {
        result.failure_reason = "publisher_start_failed";
        result.publisher_error = publisher.error();
        if(!write_evidence(options.evidence_path, make_evidence_json(options, result))) return 1;
        std::fprintf(stderr, "nozzle-mixer smoke forward FAIL: %s: %s\n", result.failure_reason.c_str(), result.publisher_error.c_str());
        return 1;
    }

    nozzle::receiver_desc receiver_desc{};
    receiver_desc.name = options.source_name;
    receiver_desc.application_name = "nozzle-mixer smoke forward";
    receiver_desc.receive_mode_val = nozzle::receive_mode::sequential_best_effort;

    std::unique_ptr<nozzle::receiver> receiver{};
    compositor_params params{};
    params.mode = mixer_mode::cut_a;

    const auto start = std::chrono::steady_clock::now();
    while(result.published_count < options.publish_frames) {
        const auto now = std::chrono::steady_clock::now();
        const uint64_t elapsed_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if(options.timeout_ms <= elapsed_ms) {
            result.failure_reason = receiver ? "timeout_waiting_for_publish_frames" : "receiver_create_timeout";
            break;
        }

        if(!receiver) {
            auto receiver_result = nozzle::receiver::create(receiver_desc);
            if(!receiver_result.ok()) {
                result.create_error = receiver_result.error().message;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            receiver = std::make_unique<nozzle::receiver>(std::move(receiver_result.value()));
            result.create_error.clear();
        }

        nozzle::acquire_desc acquire_desc{};
        acquire_desc.timeout_ms = 100u;
        auto frame_result = receiver->acquire_frame(acquire_desc);
        if(!frame_result.ok()) {
            result.acquire_error = frame_result.error().message;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        result.acquire_error.clear();

        nozzle::frame frame = std::move(frame_result.value());
        const nozzle::frame_info info = frame.info();
        result.observed_count = result.observed_count + 1u;
        result.observed_width = info.width;
        result.observed_height = info.height;
        result.last_frame_index = info.frame_index;
        if(result.observed_frame_indices.empty() || result.observed_frame_indices.back() != info.frame_index) {
            result.observed_frame_indices.push_back(info.frame_index);
            result.distinct_count = (uint64_t)result.observed_frame_indices.size();
        }
        result.sender_info = receiver->connected_info();

        if(info.width != options.width || info.height != options.height) {
            result.failure_reason = "dimension_mismatch";
            break;
        }

        gpu_frame_ref ref = make_gpu_frame_ref(frame);
        if(!ref.usable) {
            result.failure_reason = "input_gpu_texture_unavailable";
            break;
        }
        if(!compositor->resize(options.width, options.height)) {
            result.failure_reason = "compositor_resize_failed";
            result.compositor_error = compositor->last_error();
            break;
        }
        if(!compositor->render(&ref, nullptr, params)) {
            result.failure_reason = "compositor_render_failed";
            result.compositor_error = compositor->last_error();
            break;
        }
        if(!publisher.publish(*compositor)) {
            result.failure_reason = "publisher_publish_failed";
            result.publisher_error = publisher.error();
            break;
        }
        result.published_count = result.published_count + 1u;
    }

    if(result.failure_reason.empty()) {
        result.passed = options.min_frames <= result.distinct_count && 0 < result.published_count;
        if(!result.passed) result.failure_reason = "minimum_distinct_frame_count_not_reached";
    }

    if(result.passed && 0 < options.hold_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(options.hold_ms));
    }

    const std::string json = make_evidence_json(options, result);
    if(!write_evidence(options.evidence_path, json)) {
        std::fprintf(stderr, "failed to write evidence: %s\n", options.evidence_path.c_str());
        return 1;
    }

    if(result.passed) {
        std::printf("nozzle-mixer smoke forward PASS %ux%u published=%llu input=%s output=%s backend=%s\n",
            result.observed_width,
            result.observed_height,
            (unsigned long long)result.published_count,
            options.source_name.c_str(),
            options.output_name.c_str(),
            result.compositor_backend.c_str());
        return 0;
    }

    std::fprintf(stderr, "nozzle-mixer smoke forward FAIL: %s\n", result.failure_reason.c_str());
    return 1;
}

} // namespace nozzle_mixer
