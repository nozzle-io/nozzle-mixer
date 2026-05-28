#pragma once

#include <gpu_compositor.hpp>

#include <nozzle/sender.hpp>

#include <memory>
#include <string>

namespace nozzle_mixer {

class output_publisher {
public:
    bool start(const std::string &name, uint32_t width, uint32_t height);
    void stop();
    bool publish(gpu_compositor &compositor);

    bool active() const noexcept;
    const std::string &status() const noexcept;
    const std::string &error() const noexcept;

private:
    std::unique_ptr<nozzle::sender> sender_{};
    std::string status_{"stopped"};
    std::string error_{};
};

} // namespace nozzle_mixer
