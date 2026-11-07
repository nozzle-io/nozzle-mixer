#pragma once

#include <gpu_compositor.hpp>

#include <nozzle/sender.hpp>

#include <memory>
#include <string>

namespace nozzle_mixer {

class output_publisher {
public:
    // The injected device/context must outlive this publisher's active sender.
    bool start(const std::string &name, uint32_t width, uint32_t height,
        const nozzle::native_device_desc &native_device = {});
    void stop();
    nozzle::Result<void> publish(gpu_compositor &compositor);

    bool active() const noexcept;
    const std::string &status() const noexcept;
    const std::string &error() const noexcept;

private:
    std::unique_ptr<nozzle::sender> sender_{};
    std::string status_{"stopped"};
    std::string error_{};
};

} // namespace nozzle_mixer
