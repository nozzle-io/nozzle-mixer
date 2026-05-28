#include <app/mixer_state.hpp>

namespace nozzle_mixer {

namespace {

std::string source_key(const source_entry &source) {
    return source.id.empty() ? source.name : source.id;
}

bool source_exists(const source_registry &registry, const std::string &key) {
    return key.empty() || registry.find_by_id_or_name(key) != nullptr;
}

} // namespace

bool mixer_state::auto_refresh() const noexcept { return auto_refresh_; }
const std::string &mixer_state::input_a_key() const noexcept { return input_a_key_; }
const std::string &mixer_state::input_b_key() const noexcept { return input_b_key_; }
const std::string &mixer_state::output_name() const noexcept { return output_name_; }
uint32_t mixer_state::output_width() const noexcept { return output_width_; }
uint32_t mixer_state::output_height() const noexcept { return output_height_; }
mixer_mode mixer_state::mode() const noexcept { return mode_; }
float mixer_state::crossfade() const noexcept { return crossfade_; }
bool mixer_state::publishing_requested() const noexcept { return publishing_requested_; }
bool mixer_state::diagnostic_previews_enabled() const noexcept { return diagnostic_previews_enabled_; }

void mixer_state::set_auto_refresh(bool enabled) noexcept { auto_refresh_ = enabled; }
void mixer_state::set_input_a_key(const std::string &key) { input_a_key_ = key; }
void mixer_state::set_input_b_key(const std::string &key) { input_b_key_ = key; }
void mixer_state::set_output_name(const std::string &name) { output_name_ = name.empty() ? "nozzle-mixer" : name; }

void mixer_state::set_output_size(uint32_t width, uint32_t height) noexcept {
    if(0 < width) output_width_ = width;
    if(0 < height) output_height_ = height;
}

void mixer_state::set_mode(mixer_mode mode) noexcept { mode_ = mode; }
void mixer_state::set_crossfade(float value) noexcept { crossfade_ = clamp_unit(value); }
void mixer_state::set_publishing_requested(bool enabled) noexcept { publishing_requested_ = enabled; }
void mixer_state::set_diagnostic_previews_enabled(bool enabled) noexcept { diagnostic_previews_enabled_ = enabled; }

void mixer_state::reconcile_sources(const source_registry &registry) {
    if(!source_exists(registry, input_a_key_)) input_a_key_.clear();
    if(!source_exists(registry, input_b_key_)) input_b_key_.clear();
    if(input_a_key_.empty() && !registry.sources().empty()) input_a_key_ = source_key(registry.sources().front());
}

} // namespace nozzle_mixer
