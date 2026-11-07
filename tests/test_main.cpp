#include <app/source_registry.hpp>
#include <app/mixer_state.hpp>
#include <gpu_compositor.hpp>
#include <app/smoke_forward.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if(condition) return;
    failures = failures + 1;
    std::fprintf(stderr, "FAIL: %s\n", message);
}

int argc_of(const char **argv) {
    int count = 0;
    while(argv[count] != nullptr) count = count + 1;
    return count;
}

void test_sender_snapshots_are_sorted_and_deduplicated() {
    std::vector<nozzle::sender_info> senders{
        {"zeta", "app", "2", nozzle::backend_type::metal},
        {"alpha", "app", "1", nozzle::backend_type::d3d11},
        {"alpha", "app", "1", nozzle::backend_type::d3d11},
    };

    auto entries = nozzle_mixer::to_source_entries(senders);
    expect(entries.size() == 2, "sender entries are deduplicated");
    expect(entries[0].name == "alpha", "sender entries are sorted first");
    expect(entries[1].name == "zeta", "sender entries are sorted second");
}

void test_mixer_state_reconciles_inputs() {
    nozzle_mixer::source_registry registry;
    registry.set_sources({{"camera", "app", "abc", nozzle::backend_type::metal}});

    nozzle_mixer::mixer_state state;
    state.reconcile_sources(registry);
    expect(state.input_a_key() == "abc", "input A selects the first source");
    expect(state.input_b_key().empty(), "input B remains empty");

    state.set_input_b_key("missing");
    state.reconcile_sources(registry);
    expect(state.input_b_key().empty(), "missing input B is cleared");
}

void set_input_key(nozzle_mixer::mixer_state &state, bool input_a, const std::string &key) {
    if(input_a) state.set_input_a_key(key);
    else state.set_input_b_key(key);
}

const std::string &input_key(const nozzle_mixer::mixer_state &state, bool input_a) {
    return input_a ? state.input_a_key() : state.input_b_key();
}

void test_selected_inputs_wait_and_reconnect_by_name() {
    for(bool input_a : {true, false}) {
        nozzle_mixer::source_registry registry;
        registry.set_sources({
            {"other", "app", "other-id", nozzle::backend_type::metal},
            {"camera", "app", "old-id", nozzle::backend_type::metal},
        });
        nozzle_mixer::mixer_state state;
        state.reconcile_sources(registry);
        set_input_key(state, input_a, "old-id");
        state.reconcile_sources(registry);

        registry.set_sources({{"other", "app", "other-id", nozzle::backend_type::metal}});
        state.reconcile_sources(registry);
        expect(input_key(state, input_a) == "old-id", "missing selected A/B does not switch to another source");
        registry.set_sources({});
        state.reconcile_sources(registry);
        expect(input_key(state, input_a) == "old-id", "empty discovery preserves selected A/B intent");

        registry.set_sources({
            {"other", "app", "other-id", nozzle::backend_type::metal},
            {"camera", "replacement", "new-id", nozzle::backend_type::metal},
        });
        state.reconcile_sources(registry);
        expect(input_key(state, input_a) == "new-id", "returning same-name source updates selected A/B UUID");
    }
}

void test_selected_inputs_accept_direct_uuid_replacement() {
    for(bool input_a : {true, false}) {
        nozzle_mixer::source_registry registry;
        registry.set_sources({{"camera", "app", "old-id", nozzle::backend_type::metal}});
        nozzle_mixer::mixer_state state;
        set_input_key(state, input_a, "old-id");
        state.reconcile_sources(registry);
        registry.set_sources({
            {"other", "app", "other-id", nozzle::backend_type::metal},
            {"camera", "app", "new-id", nozzle::backend_type::metal},
        });
        state.reconcile_sources(registry);
        expect(input_key(state, input_a) == "new-id", "direct same-name replacement updates A/B without an absent snapshot");
    }
}

void test_selected_inputs_ignore_uuid_name_collisions() {
    for(bool input_a : {true, false}) {
        nozzle_mixer::source_registry registry;
        registry.set_sources({{"camera", "app", "U", nozzle::backend_type::metal}});
        nozzle_mixer::mixer_state state;
        set_input_key(state, input_a, "U");
        state.reconcile_sources(registry);

        registry.set_sources({{"U", "impostor", "V", nozzle::backend_type::metal}});
        state.reconcile_sources(registry);
        expect(input_key(state, input_a) == "U", "impostor name must not steal missing A/B UUID selection");
        expect((input_a ? state.input_a_name() : state.input_b_name()) == "camera",
            "impostor name must not replace remembered A/B source name");

        registry.set_sources({
            {"U", "impostor", "V", nozzle::backend_type::metal},
            {"camera", "replacement", "new-id", nozzle::backend_type::metal},
        });
        state.reconcile_sources(registry);
        expect(input_key(state, input_a) == "new-id", "A/B reconnects camera despite colliding impostor name");
        expect((input_a ? state.input_a_name() : state.input_b_name()) == "camera",
            "A/B retains camera intent after UUID replacement");
    }
}

void test_explicit_input_clear_and_switch_replace_reconnect_intent() {
    for(bool input_a : {true, false}) {
        for(bool clear : {true, false}) {
            nozzle_mixer::source_registry registry;
            registry.set_sources({
                {"camera", "app", "old-id", nozzle::backend_type::metal},
                {"other", "app", "other-id", nozzle::backend_type::metal},
            });
            nozzle_mixer::mixer_state state;
            set_input_key(state, input_a, "old-id");
            state.reconcile_sources(registry);
            registry.set_sources({{"other", "app", "other-id", nozzle::backend_type::metal}});
            state.reconcile_sources(registry);
            set_input_key(state, input_a, clear ? "" : "other-id");
            state.reconcile_sources(registry);
            registry.set_sources({{"camera", "app", "new-id", nozzle::backend_type::metal}});
            state.reconcile_sources(registry);
            expect(input_key(state, input_a) == (clear ? "" : "other-id"),
                "return of old selection does not override explicit A/B clear or switch");
            registry.set_sources({
                {"camera", "app", "new-id", nozzle::backend_type::metal},
                {"other", "app", "other-new-id", nozzle::backend_type::metal},
            });
            state.reconcile_sources(registry);
            expect(input_key(state, input_a) == (clear ? "" : "other-new-id"),
                "A/B only reconnects the latest explicit selection");
        }
    }
}

void test_initial_auto_selection_and_explicit_none() {
    nozzle_mixer::source_registry registry;
    nozzle_mixer::mixer_state automatic;
    nozzle_mixer::mixer_state cleared;
    cleared.set_input_a_key("");
    automatic.reconcile_sources(registry);
    cleared.reconcile_sources(registry);
    registry.set_sources({{"camera", "app", "old-id", nozzle::backend_type::metal}});
    automatic.reconcile_sources(registry);
    cleared.reconcile_sources(registry);
    expect(automatic.input_a_key() == "old-id", "initial auto A waits for the first available source");
    expect(cleared.input_a_key().empty(), "explicit none prevents later auto A selection");
    registry.set_sources({{"other", "app", "other-id", nozzle::backend_type::metal}});
    automatic.reconcile_sources(registry);
    expect(automatic.input_a_key() == "old-id", "initial auto A selection is not repeated after disappearance");
}

void test_output_size_rejects_zero_dimension_updates() {
    nozzle_mixer::mixer_state state;
    state.set_output_size(1920, 1080);
    state.set_output_size(0, 720);
    expect(state.output_width() == 1920, "zero width update is ignored");
    expect(state.output_height() == 720, "non-zero height update is applied");
}

void test_crossfade_is_clamped() {
    nozzle_mixer::mixer_state state;
    state.set_crossfade(-1.0f);
    expect(state.crossfade() == 0.0f, "crossfade clamps low values");
    state.set_crossfade(2.0f);
    expect(state.crossfade() == 1.0f, "crossfade clamps high values");
}

void test_backend_and_mode_names_are_stable() {
    expect(std::string(nozzle_mixer::backend_name(nozzle::backend_type::dma_buf)) == "DMA-BUF", "DMA-BUF backend name is stable");
    expect(std::string(nozzle_mixer::format_name(nozzle::texture_format::rgba8_unorm)) == "rgba8_unorm", "rgba8 format name is stable");
    expect(std::string(nozzle_mixer::mixer_mode_name(nozzle_mixer::mixer_mode::picture_in_picture)) == "Picture in picture", "PiP mode name is stable");
}

void test_smoke_forward_option_parsing() {
    const char *argv[] = {
        "nozzle-mixer",
        "--smoke-forward",
        "--source", "input",
        "--output", "output",
        "--width", "320",
        "--height", "240",
        "--min-frames", "5",
        "--publish-frames", "7",
        "--timeout-ms", "1000",
        "--hold-ms", "0",
        "--evidence", "/tmp/evidence.json",
        nullptr
    };
    nozzle_mixer::smoke_forward_options options{};
    expect(nozzle_mixer::has_smoke_forward_request(argc_of(argv), const_cast<char **>(argv)), "smoke-forward request is detected");
    expect(nozzle_mixer::parse_smoke_forward_options(argc_of(argv), const_cast<char **>(argv), options), "smoke-forward options parse");
    expect(options.enabled, "smoke-forward is enabled");
    expect(options.source_name == "input", "source name parses");
    expect(options.output_name == "output", "output name parses");
    expect(options.width == 320, "width parses");
    expect(options.height == 240, "height parses");
    expect(options.min_frames == 5, "min frames parse");
    expect(options.publish_frames == 7, "publish frames parse");
    expect(options.timeout_ms == 1000, "timeout parses");
    expect(options.hold_ms == 0, "hold parses");
    expect(options.evidence_path == "/tmp/evidence.json", "evidence path parses");
}

void test_smoke_forward_rejects_bad_options() {
    const char *unknown[] = {"nozzle-mixer", "--smoke-forward", "--garbage", nullptr};
    nozzle_mixer::smoke_forward_options options{};
    expect(!nozzle_mixer::parse_smoke_forward_options(argc_of(unknown), const_cast<char **>(unknown), options), "unknown smoke option is rejected");

    const char *missing_value[] = {"nozzle-mixer", "--smoke-forward", "--source", nullptr};
    options = {};
    expect(!nozzle_mixer::parse_smoke_forward_options(argc_of(missing_value), const_cast<char **>(missing_value), options), "missing option value is rejected");

    const char *negative_timeout[] = {"nozzle-mixer", "--smoke-forward", "--timeout-ms", "-1", nullptr};
    options = {};
    expect(!nozzle_mixer::parse_smoke_forward_options(argc_of(negative_timeout), const_cast<char **>(negative_timeout), options), "negative integer option is rejected");

    const char *overflow_timeout[] = {"nozzle-mixer", "--smoke-forward", "--timeout-ms", "184467440737095516160", nullptr};
    options = {};
    expect(!nozzle_mixer::parse_smoke_forward_options(argc_of(overflow_timeout), const_cast<char **>(overflow_timeout), options), "overflow integer option is rejected");

    const char *spaced_negative_timeout[] = {"nozzle-mixer", "--smoke-forward", "--timeout-ms", " -1", nullptr};
    options = {};
    expect(!nozzle_mixer::parse_smoke_forward_options(argc_of(spaced_negative_timeout), const_cast<char **>(spaced_negative_timeout), options), "space-prefixed negative integer option is rejected");

    const char *spaced_number_timeout[] = {"nozzle-mixer", "--smoke-forward", "--timeout-ms", " 1", nullptr};
    options = {};
    expect(!nozzle_mixer::parse_smoke_forward_options(argc_of(spaced_number_timeout), const_cast<char **>(spaced_number_timeout), options), "space-prefixed integer option is rejected");

    const char *gui_unknown[] = {"nozzle-mixer", "--future-gui-option", nullptr};
    expect(!nozzle_mixer::has_smoke_forward_request(argc_of(gui_unknown), const_cast<char **>(gui_unknown)), "GUI-only options are not parsed as smoke-forward options");
}

void test_metal_fallback_presence_is_not_texture_size() {
#if defined(__APPLE__) && defined(NOZZLE_MIXER_SOURCE_DIR)
    const std::string path = std::string(NOZZLE_MIXER_SOURCE_DIR) + "/src/gpu/platform/macos/gpu_compositor_metal.mm";
    std::ifstream file(path);
    expect(file.good(), "Metal compositor source is readable for fallback-presence regression guard");
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string source = buffer.str();
    expect(source.find("uint has_a;") != std::string::npos, "Metal shader has explicit has_a parameter");
    expect(source.find("uint has_b;") != std::string::npos, "Metal shader has explicit has_b parameter");
    expect(source.find("params.has_a != 0") != std::string::npos, "Metal shader reads explicit has_a parameter");
    expect(source.find("params.has_b != 0") != std::string::npos, "Metal shader reads explicit has_b parameter");
    expect(source.find("input_a.get_width() > 0") == std::string::npos, "Metal shader does not use texture width as input A presence");
    expect(source.find("input_b.get_width() > 0") == std::string::npos, "Metal shader does not use texture width as input B presence");
    expect(source.find("metal.has_a = has_a ? 1u : 0u;") != std::string::npos, "Metal renderer writes explicit has_a parameter");
    expect(source.find("metal.has_b = has_b ? 1u : 0u;") != std::string::npos, "Metal renderer writes explicit has_b parameter");
#endif
}

} // namespace

int main() {
    test_sender_snapshots_are_sorted_and_deduplicated();
    test_mixer_state_reconciles_inputs();
    test_selected_inputs_wait_and_reconnect_by_name();
    test_selected_inputs_accept_direct_uuid_replacement();
    test_selected_inputs_ignore_uuid_name_collisions();
    test_explicit_input_clear_and_switch_replace_reconnect_intent();
    test_initial_auto_selection_and_explicit_none();
    test_output_size_rejects_zero_dimension_updates();
    test_crossfade_is_clamped();
    test_backend_and_mode_names_are_stable();
    test_smoke_forward_option_parsing();
    test_smoke_forward_rejects_bad_options();
    test_metal_fallback_presence_is_not_texture_size();
    return failures == 0 ? 0 : 1;
}
