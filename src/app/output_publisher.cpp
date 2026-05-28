#include <app/output_publisher.hpp>

namespace nozzle_mixer {

bool output_publisher::start(const std::string &name, uint32_t width, uint32_t height) {
    if(name.empty() || width == 0 || height == 0) {
        status_ = "invalid output settings";
        error_ = "output name and size are required";
        return false;
    }

    nozzle::sender_desc desc{};
    desc.name = name;
    desc.application_name = "nozzle-mixer";
    desc.ring_buffer_size = 3;

    auto result = nozzle::sender::create(desc);
    if(!result.ok()) {
        status_ = "sender create failed";
        error_ = result.error().message;
        sender_.reset();
        return false;
    }

    sender_ = std::make_unique<nozzle::sender>(std::move(result.value()));
    status_ = "publishing";
    error_.clear();
    return true;
}

void output_publisher::stop() {
    sender_.reset();
    status_ = "stopped";
    error_.clear();
}

bool output_publisher::publish(gpu_compositor &compositor) {
    if(!sender_) {
        status_ = "stopped";
        return false;
    }
    if(!compositor.publish(*sender_)) {
        status_ = "publish failed";
        error_ = compositor.last_error();
        return false;
    }
    status_ = "publishing";
    error_.clear();
    return true;
}

bool output_publisher::active() const noexcept {
    return sender_ != nullptr;
}

const std::string &output_publisher::status() const noexcept {
    return status_;
}

const std::string &output_publisher::error() const noexcept {
    return error_;
}

} // namespace nozzle_mixer
