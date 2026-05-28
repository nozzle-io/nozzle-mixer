#pragma once

#include <app/mixer_state.hpp>
#include <app/output_publisher.hpp>
#include <app/source_registry.hpp>
#include <gpu_compositor.hpp>
#include <gui/render_backend.hpp>

#include <nozzle/frame.hpp>
#include <nozzle/receiver.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

struct GLFWwindow;

namespace nozzle_mixer {

struct receiver_session {
    source_entry source{};
    std::unique_ptr<nozzle::receiver> receiver{};
    std::unique_ptr<nozzle::frame> current_frame{};
    gpu_frame_ref gpu_ref{};
    void *preview_texture{nullptr};
    uint32_t preview_width{0};
    uint32_t preview_height{0};
    nozzle::connected_sender_info connected_info{};
    std::string status{"not connected"};
    std::string error{};
    uint64_t last_frame_index{0};
    bool connected{false};
};

class gui {
public:
    gui();
    ~gui();

    gui(const gui &) = delete;
    gui &operator=(const gui &) = delete;

    bool init();
    void run();
    void shutdown();

private:
    void refresh_sources();
    void update_sessions();
    void update_mixer();
    void reconcile_sessions();
    void connect_session(receiver_session &session);
    void acquire_frame(receiver_session &session);
    void draw_ui();
    void draw_toolbar();
    void draw_input_selector(const char *label, bool input_a);
    void draw_source_tile(receiver_session &session, float width, float height);
    void destroy_session_texture(receiver_session &session);
    receiver_session *session_for_key(const std::string &key);

    GLFWwindow *window_{nullptr};
    std::unique_ptr<render_backend> backend_{};
    std::unique_ptr<gpu_compositor> compositor_{};
    output_publisher publisher_{};
    source_registry registry_{};
    mixer_state state_{};
    std::unordered_map<std::string, receiver_session> sessions_{};
    std::chrono::steady_clock::time_point last_refresh_{};
    bool running_{false};
};

} // namespace nozzle_mixer
