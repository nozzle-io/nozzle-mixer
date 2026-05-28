#pragma once

#include <app/source_registry.hpp>
#include <gpu_compositor.hpp>

#include <cstdint>
#include <string>

namespace nozzle_mixer {

class mixer_state {
public:
    bool auto_refresh() const noexcept;
    const std::string &input_a_key() const noexcept;
    const std::string &input_b_key() const noexcept;
    const std::string &output_name() const noexcept;
    uint32_t output_width() const noexcept;
    uint32_t output_height() const noexcept;
    mixer_mode mode() const noexcept;
    float crossfade() const noexcept;
    bool publishing_requested() const noexcept;

    void set_auto_refresh(bool enabled) noexcept;
    void set_input_a_key(const std::string &key);
    void set_input_b_key(const std::string &key);
    void set_output_name(const std::string &name);
    void set_output_size(uint32_t width, uint32_t height) noexcept;
    void set_mode(mixer_mode mode) noexcept;
    void set_crossfade(float value) noexcept;
    void set_publishing_requested(bool enabled) noexcept;
    void reconcile_sources(const source_registry &registry);

private:
    bool auto_refresh_{true};
    std::string input_a_key_{};
    std::string input_b_key_{};
    std::string output_name_{"nozzle-mixer"};
    uint32_t output_width_{1280};
    uint32_t output_height_{720};
    mixer_mode mode_{mixer_mode::cut_a};
    float crossfade_{0.0f};
    bool publishing_requested_{false};
};

} // namespace nozzle_mixer
