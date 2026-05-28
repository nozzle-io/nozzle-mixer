#include <gpu_compositor.hpp>

namespace nozzle_mixer {

const char *mixer_mode_name(mixer_mode mode) noexcept {
    switch(mode) {
        case mixer_mode::cut_a: return "Cut A";
        case mixer_mode::cut_b: return "Cut B";
        case mixer_mode::crossfade: return "Crossfade";
        case mixer_mode::side_by_side: return "Side by side";
        case mixer_mode::picture_in_picture: return "Picture in picture";
        case mixer_mode::solid_color: return "Solid color";
    }
    return "Unknown";
}

float clamp_unit(float value) noexcept {
    if(value < 0.0f) return 0.0f;
    if(1.0f < value) return 1.0f;
    return value;
}

} // namespace nozzle_mixer
