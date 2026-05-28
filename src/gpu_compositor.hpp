#pragma once

#include <nozzle/frame.hpp>
#include <nozzle/sender.hpp>
#include <nozzle/types.hpp>

#include <cstdint>
#include <memory>

namespace nozzle_mixer {

enum class mixer_mode {
    cut_a,
    cut_b,
    crossfade,
    side_by_side,
    picture_in_picture,
    solid_color,
};

struct gpu_frame_ref {
    void *native_texture{nullptr};
    uint32_t width{0};
    uint32_t height{0};
    nozzle::texture_format format{nozzle::texture_format::unknown};
    nozzle::backend_type backend{nozzle::backend_type::unknown};
    bool usable{false};
};

struct compositor_params {
    mixer_mode mode{mixer_mode::cut_a};
    float crossfade{0.0f};
    float pip_scale{0.30f};
    float pip_margin_x{0.04f};
    float pip_margin_y{0.04f};
    float solid_r{0.0f};
    float solid_g{0.0f};
    float solid_b{0.0f};
    float solid_a{1.0f};
};

class gpu_compositor {
public:
    virtual ~gpu_compositor() = default;

    virtual bool init(uint32_t width, uint32_t height) = 0;
    virtual bool resize(uint32_t width, uint32_t height) = 0;
    virtual bool render(const gpu_frame_ref *input_a, const gpu_frame_ref *input_b, const compositor_params &params) = 0;
    virtual bool publish(nozzle::sender &sender) = 0;
    virtual void *preview_texture_handle() const = 0;
    virtual const char *backend_name() const = 0;
    virtual const char *last_error() const = 0;
};

std::unique_ptr<gpu_compositor> create_gpu_compositor();
gpu_frame_ref make_gpu_frame_ref(const nozzle::frame &frame);
const char *mixer_mode_name(mixer_mode mode) noexcept;
float clamp_unit(float value) noexcept;

} // namespace nozzle_mixer
