#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <app/output_publisher.hpp>
#include <gpu_compositor.hpp>
#include <nozzle/pixel_access.hpp>
#include <nozzle/receiver.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t width = 4;
constexpr uint32_t height = 3;

struct color_case {
    float red;
    float green;
    float blue;
    std::array<uint8_t, 4> expected_bgra;
};

// Fixed readback bytes, not an oracle derived from the compositor implementation.
constexpr color_case colors[] = {
    {1.0f, 0.25f, 0.0f, {0, 64, 255, 255}},
    {0.0f, 1.0f, 0.75f, {191, 255, 0, 255}},
    {0.5f, 0.0f, 1.0f, {255, 0, 128, 255}},
};

class receiver_child {
public:
    ~receiver_child() {
        if(process_.hProcess) {
            if(WaitForSingleObject(process_.hProcess, 0) != WAIT_OBJECT_0) {
                TerminateProcess(process_.hProcess, 1);
                WaitForSingleObject(process_.hProcess, 5000);
            }
            CloseHandle(process_.hProcess);
        }
        if(process_.hThread) CloseHandle(process_.hThread);
        if(job_) CloseHandle(job_);
    }

    bool run(const std::string &name, unsigned phase) {
        std::vector<wchar_t> executable(32768);
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
            static_cast<DWORD>(executable.size()));
        if(length == 0 || length >= executable.size()) return fail("GetModuleFileNameW");
        std::wstring command = L"\"" + std::wstring(executable.data(), length) +
            L"\" --receive " + std::wstring(name.begin(), name.end()) + L" " + std::to_wstring(phase);

        // Also reap the child if CTest terminates the parent on its outer timeout.
        job_ = CreateJobObjectW(nullptr, nullptr);
        if(!job_) return fail("CreateJobObjectW");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            return fail("SetInformationJobObject");
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        if(!CreateProcessW(executable.data(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED, nullptr, nullptr, &startup, &process_)) return fail("CreateProcessW");
        if(!AssignProcessToJobObject(job_, process_.hProcess)) return fail("AssignProcessToJobObject");
        if(ResumeThread(process_.hThread) == static_cast<DWORD>(-1)) return fail("ResumeThread");
        const DWORD waited = WaitForSingleObject(process_.hProcess, 10000);
        if(waited != WAIT_OBJECT_0) {
            std::fprintf(stderr, "receiver phase %u did not finish: wait=%lu\n", phase, waited);
            return false;
        }
        DWORD exit_code = 1;
        if(!GetExitCodeProcess(process_.hProcess, &exit_code)) return fail("GetExitCodeProcess");
        if(exit_code != 0) {
            std::fprintf(stderr, "receiver phase %u failed: exit=%lu\n", phase, exit_code);
            return false;
        }
        return true;
    }

private:
    bool fail(const char *operation) {
        std::fprintf(stderr, "%s failed: Win32=%lu\n", operation, GetLastError());
        return false;
    }

    HANDLE job_{nullptr};
    PROCESS_INFORMATION process_{};
};

int receive(const char *name, unsigned phase) {
    nozzle::receiver_desc desc{};
    desc.name = name;
    desc.application_name = "mixer-output-regression";
    auto created = nozzle::receiver::create(desc);
    if(!created.ok()) {
        std::fprintf(stderr, "receiver create: %s\n", created.error().message.c_str());
        return 1;
    }
    auto receiver = std::move(created.value());
    nozzle::acquire_desc acquire{};
    acquire.timeout_ms = 2000;
    auto acquired = receiver.acquire_frame(acquire);
    if(!acquired.ok()) {
        std::fprintf(stderr, "receiver acquire: %s\n", acquired.error().message.c_str());
        return 1;
    }
    auto frame = std::move(acquired.value());
    const auto info = frame.info();
    const auto connected = receiver.connected_info();
    const uint64_t expected_index = phase == 1 ? 2u : 1u;
    if(connected.name != name || connected.application_name != "nozzle-mixer" ||
        connected.backend != nozzle::backend_type::d3d11 || info.width != width ||
        info.height != height || info.format != nozzle::texture_format::bgra8_unorm ||
        info.frame_index != expected_index) {
        std::fprintf(stderr, "receiver identity/dimensions/format/frame-index mismatch, phase=%u\n", phase);
        return 1;
    }
    auto mapped = nozzle::lock_frame_pixels_mapping_with_origin(frame, nozzle::texture_origin::top_left);
    if(!mapped.ok()) {
        std::fprintf(stderr, "receiver mapping: %s\n", mapped.error().message.c_str());
        return 1;
    }
    auto mapping = std::move(mapped.value());
    const auto &pixels = mapping.pixels();
    if(!pixels.data || pixels.width != width || pixels.height != height ||
        pixels.format != nozzle::texture_format::bgra8_unorm ||
        pixels.origin != nozzle::texture_origin::top_left ||
        pixels.row_stride_bytes < static_cast<std::ptrdiff_t>(width * 4)) {
        std::fprintf(stderr, "receiver mapping layout mismatch\n");
        return 1;
    }
    const auto *base = static_cast<const uint8_t *>(pixels.data);
    for(uint32_t y = 0; y < height; ++y) {
        const auto *row = base + static_cast<std::ptrdiff_t>(y) * pixels.row_stride_bytes;
        for(uint32_t x = 0; x < width; ++x) {
            if(std::memcmp(row + x * 4, colors[phase].expected_bgra.data(), 4) != 0) {
                std::fprintf(stderr, "pixel mismatch phase=%u x=%u y=%u BGRA=%u,%u,%u,%u\n",
                    phase, x, y, row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]);
                return 1;
            }
        }
    }
    auto unlocked = mapping.unlock_checked();
    if(!unlocked.ok()) {
        std::fprintf(stderr, "receiver unlock: %s\n", unlocked.error().message.c_str());
        return 1;
    }
    return 0;
}

int produce() {
    auto compositor = nozzle_mixer::create_gpu_compositor();
    if(!compositor || !compositor->init(width, height)) {
        std::fprintf(stderr, "compositor init: %s\n", compositor ? compositor->last_error() : "null");
        return 1;
    }
    const auto native = compositor->native_device();
    if(native.backend != nozzle::backend_type::d3d11 || !native.device || !native.context) {
        std::fprintf(stderr, "compositor did not expose its owned D3D11 device/context\n");
        return 1;
    }
    // Declaration order keeps the device owner alive until all senders are gone.
    nozzle_mixer::output_publisher publisher;
    const std::string name = "mixer-output-" + std::to_string(GetCurrentProcessId()) +
        "-" + std::to_string(GetTickCount64());
    for(unsigned phase = 0; phase < 3; ++phase) {
        if(phase == 2) publisher.stop();
        if(phase != 1 && !publisher.start(name, width, height, compositor->native_device())) {
            std::fprintf(stderr, "publisher start: %s\n", publisher.error().c_str());
            return 1;
        }
        if(compositor->native_device().device != native.device ||
            compositor->native_device().context != native.context) {
            std::fprintf(stderr, "publisher restart changed compositor ownership\n");
            return 1;
        }
        nozzle_mixer::compositor_params params{};
        params.mode = nozzle_mixer::mixer_mode::solid_color;
        params.solid_r = colors[phase].red;
        params.solid_g = colors[phase].green;
        params.solid_b = colors[phase].blue;
        params.solid_a = 1.0f;
        if(!compositor->render(nullptr, nullptr, params)) {
            std::fprintf(stderr, "compositor render: %s\n", compositor->last_error());
            return 1;
        }
        auto published = publisher.publish(*compositor);
        if(!published.ok()) {
            std::fprintf(stderr, "publisher phase=%u code=%u: %s\n", phase,
                static_cast<unsigned>(published.error().code), published.error().message.c_str());
            return 1;
        }
        // A fresh process imports the ring on its own device. No reconnect claim.
        receiver_child child;
        if(!child.run(name, phase)) return 1;
        std::printf("PASS D3D11 output phase=%u frame=%u full-image BGRA verified\n",
            phase, phase == 1 ? 2u : 1u);
    }
    publisher.stop();
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if(argc == 1) return produce();
    if(argc == 4 && std::strcmp(argv[1], "--receive") == 0 &&
        std::strlen(argv[3]) == 1 && argv[3][0] >= '0' && argv[3][0] <= '2') {
        return receive(argv[2], static_cast<unsigned>(argv[3][0] - '0'));
    }
    std::fprintf(stderr, "usage: nozzle_mixer_d3d11_output_test [--receive NAME PHASE]\n");
    return 2;
}
