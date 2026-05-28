#include <app/source_registry.hpp>
#include <app/mixer_state.hpp>
#include <gpu_compositor.hpp>

#include <cassert>
#include <string>
#include <vector>

namespace {

void test_sender_snapshots_are_sorted_and_deduplicated() {
    std::vector<nozzle::sender_info> senders{
        {"zeta", "app", "2", nozzle::backend_type::metal},
        {"alpha", "app", "1", nozzle::backend_type::d3d11},
        {"alpha", "app", "1", nozzle::backend_type::d3d11},
    };

    auto entries = nozzle_mixer::to_source_entries(senders);
    assert(entries.size() == 2);
    assert(entries[0].name == "alpha");
    assert(entries[1].name == "zeta");
}

void test_mixer_state_reconciles_inputs() {
    nozzle_mixer::source_registry registry;
    registry.set_sources({{"camera", "app", "abc", nozzle::backend_type::metal}});

    nozzle_mixer::mixer_state state;
    state.reconcile_sources(registry);
    assert(state.input_a_key() == "abc");
    assert(state.input_b_key().empty());

    state.set_input_b_key("missing");
    state.reconcile_sources(registry);
    assert(state.input_b_key().empty());
}

void test_output_size_rejects_zero_dimension_updates() {
    nozzle_mixer::mixer_state state;
    state.set_output_size(1920, 1080);
    state.set_output_size(0, 720);
    assert(state.output_width() == 1920);
    assert(state.output_height() == 720);
}

void test_crossfade_is_clamped() {
    nozzle_mixer::mixer_state state;
    state.set_crossfade(-1.0f);
    assert(state.crossfade() == 0.0f);
    state.set_crossfade(2.0f);
    assert(state.crossfade() == 1.0f);
}

void test_backend_and_mode_names_are_stable() {
    assert(std::string(nozzle_mixer::backend_name(nozzle::backend_type::dma_buf)) == "DMA-BUF");
    assert(std::string(nozzle_mixer::format_name(nozzle::texture_format::rgba8_unorm)) == "rgba8_unorm");
    assert(std::string(nozzle_mixer::mixer_mode_name(nozzle_mixer::mixer_mode::picture_in_picture)) == "Picture in picture");
}

} // namespace

int main() {
    test_sender_snapshots_are_sorted_and_deduplicated();
    test_mixer_state_reconciles_inputs();
    test_output_size_rejects_zero_dimension_updates();
    test_crossfade_is_clamped();
    test_backend_and_mode_names_are_stable();
    return 0;
}
