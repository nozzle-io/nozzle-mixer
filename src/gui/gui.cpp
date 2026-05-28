#include <gui/gui.hpp>

#include <nozzle/pixel_access.hpp>

#include <imgui.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nozzle_mixer {

namespace {

constexpr float k_window_default_w = 1180.0f;
constexpr float k_window_default_h = 760.0f;
constexpr auto k_refresh_interval = std::chrono::milliseconds(1000);

std::string source_key(const source_entry &source) {
    return source.id.empty() ? source.name : source.id;
}

preview_format preview_format_from_nozzle(nozzle::texture_format format, bool *supported) {
    *supported = true;
    switch(format) {
        case nozzle::texture_format::rgba8_unorm:
        case nozzle::texture_format::rgba8_srgb: return preview_format::rgba8;
        case nozzle::texture_format::bgra8_unorm:
        case nozzle::texture_format::bgra8_srgb: return preview_format::bgra8;
        default:
            *supported = false;
            return preview_format::rgba8;
    }
}

} // namespace

gui::gui() = default;

gui::~gui() {
    shutdown();
}

bool gui::init() {
    if(!glfwInit()) return false;

    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    const int win_w = mode ? std::min((int)k_window_default_w, mode->width * 3 / 4) : (int)k_window_default_w;
    const int win_h = mode ? std::min((int)k_window_default_h, mode->height * 3 / 4) : (int)k_window_default_h;

#if !defined(__linux__)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(win_w, win_h, "nozzle-mixer", nullptr, nullptr);
    if(!window_) {
        glfwTerminate();
        return false;
    }

    if(mode) {
        int monitor_x = 0;
        int monitor_y = 0;
        glfwGetMonitorPos(monitor, &monitor_x, &monitor_y);
        glfwSetWindowPos(window_, monitor_x + (mode->width - win_w) / 2, monitor_y + (mode->height - win_h) / 2);
    }

    backend_ = create_render_backend();
    compositor_ = create_gpu_compositor();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImFontConfig font_config;
    font_config.OversampleH = 2;
    font_config.OversampleV = 1;
    const char *font_path = backend_->get_system_font_path();
    if(font_path && font_path[0] != '\0') {
        io.Fonts->AddFontFromFileTTF(font_path, 16.0f, &font_config, io.Fonts->GetGlyphRangesJapanese());
    } else {
        io.Fonts->AddFontDefault();
    }
    ImGui::StyleColorsDark();

    if(!backend_->init(window_)) {
        ImGui::DestroyContext();
        glfwDestroyWindow(window_);
        glfwTerminate();
        window_ = nullptr;
        return false;
    }

    compositor_->init(state_.output_width(), state_.output_height());
    refresh_sources();
    return true;
}

void gui::run() {
    running_ = true;
    while(running_ && window_ && !glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        int framebuffer_w = 0;
        int framebuffer_h = 0;
        glfwGetFramebufferSize(window_, &framebuffer_w, &framebuffer_h);
        if(framebuffer_w <= 0 || framebuffer_h <= 0) continue;
        update_sessions();
        update_mixer();
        backend_->begin_frame();
        draw_ui();
        backend_->end_frame();
    }
}

void gui::shutdown() {
    running_ = false;
    publisher_.stop();
    if(backend_) {
        for(auto &pair : sessions_) destroy_session_texture(pair.second);
        sessions_.clear();
    }
    if(window_) {
        backend_->shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window_);
        glfwTerminate();
        window_ = nullptr;
    }
}

void gui::refresh_sources() {
    registry_.refresh();
    state_.reconcile_sources(registry_);
    reconcile_sessions();
    last_refresh_ = std::chrono::steady_clock::now();
}

void gui::update_sessions() {
    const auto now = std::chrono::steady_clock::now();
    if(state_.auto_refresh() && k_refresh_interval <= now - last_refresh_) refresh_sources();
    for(auto &pair : sessions_) {
        auto &session = pair.second;
        if(!session.connected) connect_session(session);
        if(session.connected) acquire_frame(session);
    }
}

void gui::update_mixer() {
    if(!compositor_) return;
    receiver_session *input_a = session_for_key(state_.input_a_key());
    receiver_session *input_b = session_for_key(state_.input_b_key());
    compositor_params params{};
    params.mode = state_.mode();
    params.crossfade = state_.crossfade();
    const gpu_frame_ref *ref_a = input_a && input_a->gpu_ref.usable ? &input_a->gpu_ref : nullptr;
    const gpu_frame_ref *ref_b = input_b && input_b->gpu_ref.usable ? &input_b->gpu_ref : nullptr;
    compositor_->resize(state_.output_width(), state_.output_height());
    compositor_->render(ref_a, ref_b, params);
    if(state_.publishing_requested() && !publisher_.active()) {
        publisher_.start(state_.output_name(), state_.output_width(), state_.output_height());
    } else if(!state_.publishing_requested() && publisher_.active()) {
        publisher_.stop();
    }
    if(publisher_.active()) publisher_.publish(*compositor_);
}

void gui::reconcile_sessions() {
    for(const auto &source : registry_.sources()) {
        const auto key = source_key(source);
        auto it = sessions_.find(key);
        if(it == sessions_.end()) {
            receiver_session session{};
            session.source = source;
            sessions_.emplace(key, std::move(session));
        } else {
            it->second.source = source;
        }
    }
    for(auto it = sessions_.begin(); it != sessions_.end();) {
        if(!registry_.find_by_id_or_name(it->first)) {
            destroy_session_texture(it->second);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void gui::connect_session(receiver_session &session) {
    nozzle::receiver_desc desc{};
    desc.name = session.source.name;
    desc.application_name = "nozzle-mixer";
    auto result = nozzle::receiver::create(desc);
    if(!result.ok()) {
        session.connected = false;
        session.status = "receiver unavailable";
        session.error = result.error().message;
        return;
    }
    session.receiver = std::make_unique<nozzle::receiver>(std::move(result.value()));
    session.connected = true;
    session.connected_info = session.receiver->connected_info();
    session.status = "connected";
    session.error.clear();
}

void gui::acquire_frame(receiver_session &session) {
    if(!session.receiver || !session.receiver->valid()) {
        session.connected = false;
        session.status = "disconnected";
        return;
    }
    nozzle::acquire_desc desc{};
    desc.timeout_ms = 0;
    auto frame_result = session.receiver->acquire_frame(desc);
    if(!frame_result.ok()) {
        if(frame_result.error().code == nozzle::ErrorCode::Timeout) {
            session.status = "waiting for frame";
        } else {
            session.status = "acquire failed";
            session.error = frame_result.error().message;
            if(frame_result.error().code == nozzle::ErrorCode::SenderClosed || frame_result.error().code == nozzle::ErrorCode::SenderNotFound) {
                session.connected = false;
                session.receiver.reset();
            }
        }
        return;
    }

    session.current_frame = std::make_unique<nozzle::frame>(std::move(frame_result.value()));
    nozzle::frame &frame = *session.current_frame;
    auto info = frame.info();
    session.gpu_ref = make_gpu_frame_ref(frame);
    session.connected_info = session.receiver->connected_info();
    session.last_frame_index = info.frame_index;

    bool supported = false;
    const auto preview_format = preview_format_from_nozzle(info.format, &supported);
    if(!supported) {
        session.status = session.gpu_ref.usable ? "GPU input only; preview unsupported" : "unsupported input format";
        session.error = format_name(info.format);
        return;
    }
    auto pixels_result = nozzle::lock_frame_pixels_with_origin(frame, nozzle::texture_origin::top_left);
    if(!pixels_result.ok()) {
        session.status = session.gpu_ref.usable ? "GPU input live; preview readback failed" : "pixel readback failed";
        session.error = pixels_result.error().message;
        return;
    }
    const auto &pixels = pixels_result.value();
    if(!session.preview_texture || session.preview_width != pixels.width || session.preview_height != pixels.height) {
        destroy_session_texture(session);
        session.preview_texture = backend_->create_preview_texture(pixels.width, pixels.height);
        session.preview_width = pixels.width;
        session.preview_height = pixels.height;
    }
    if(session.preview_texture && backend_->update_preview_texture(session.preview_texture, pixels.data, pixels.width, pixels.height, pixels.row_stride_bytes, preview_format)) {
        session.status = session.gpu_ref.usable ? "GPU input live" : "CPU preview only";
        session.error.clear();
    }
    nozzle::unlock_frame_pixels(frame);
}

void gui::draw_ui() {
    int framebuffer_w = 0;
    int framebuffer_h = 0;
    glfwGetFramebufferSize(window_, &framebuffer_w, &framebuffer_h);
    ImGui::SetNextWindowSize(ImVec2((float)framebuffer_w, (float)framebuffer_h));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::Begin("nozzle-mixer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    draw_toolbar();
    ImGui::Separator();
    draw_input_selector("Input A", true);
    draw_input_selector("Input B", false);
    ImGui::Separator();

    const float content_w = ImGui::GetContentRegionAvail().x;
    const float tile_w = (content_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    receiver_session *input_a = session_for_key(state_.input_a_key());
    receiver_session *input_b = session_for_key(state_.input_b_key());
    if(input_a) draw_source_tile(*input_a, tile_w, 300.0f);
    ImGui::SameLine();
    if(input_b) draw_source_tile(*input_b, tile_w, 300.0f);
    ImGui::End();
}

void gui::draw_toolbar() {
    if(ImGui::Button("Refresh")) refresh_sources();
    ImGui::SameLine();
    bool auto_refresh = state_.auto_refresh();
    if(ImGui::Checkbox("Auto refresh", &auto_refresh)) state_.set_auto_refresh(auto_refresh);
    ImGui::SameLine();
    ImGui::Text("Sources: %d", (int)registry_.sources().size());

    char output_name[128]{};
    std::snprintf(output_name, sizeof(output_name), "%s", state_.output_name().c_str());
    if(ImGui::InputText("Output name", output_name, sizeof(output_name))) state_.set_output_name(output_name);
    int output_width = (int)state_.output_width();
    int output_height = (int)state_.output_height();
    if(ImGui::InputInt("Output width", &output_width)) state_.set_output_size((uint32_t)std::max(1, output_width), state_.output_height());
    if(ImGui::InputInt("Output height", &output_height)) state_.set_output_size(state_.output_width(), (uint32_t)std::max(1, output_height));

    const char *items[] = {"Cut A", "Cut B", "Crossfade", "Side by side", "Picture in picture", "Solid color"};
    int current = (int)state_.mode();
    if(ImGui::Combo("Mode", &current, items, 6)) state_.set_mode((mixer_mode)current);
    float crossfade = state_.crossfade();
    if(ImGui::SliderFloat("Crossfade", &crossfade, 0.0f, 1.0f)) state_.set_crossfade(crossfade);
    bool publish = state_.publishing_requested();
    if(ImGui::Checkbox("Publish output", &publish)) state_.set_publishing_requested(publish);
    ImGui::TextDisabled("GPU backend: %s", compositor_ ? compositor_->backend_name() : "none");
    ImGui::Text("Publisher: %s", publisher_.status().c_str());
    if(compositor_ && compositor_->last_error()[0] != '\0') ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", compositor_->last_error());
    if(!publisher_.error().empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", publisher_.error().c_str());
}

void gui::draw_input_selector(const char *label, bool input_a) {
    const std::string &current_key = input_a ? state_.input_a_key() : state_.input_b_key();
    std::string preview = current_key.empty() ? "<none>" : current_key;
    if(ImGui::BeginCombo(label, preview.c_str())) {
        if(ImGui::Selectable("<none>", current_key.empty())) {
            if(input_a) state_.set_input_a_key(""); else state_.set_input_b_key("");
        }
        for(const auto &source : registry_.sources()) {
            const auto key = source_key(source);
            if(ImGui::Selectable(source.name.c_str(), key == current_key)) {
                if(input_a) state_.set_input_a_key(key); else state_.set_input_b_key(key);
            }
        }
        ImGui::EndCombo();
    }
}

void gui::draw_source_tile(receiver_session &session, float width, float height) {
    ImGui::PushID(source_key(session.source).c_str());
    ImGui::BeginChild("tile", ImVec2(width, height), true);
    ImGui::TextUnformatted(session.source.name.c_str());
    ImGui::TextDisabled("Backend: %s", backend_name(session.source.backend));
    ImGui::TextDisabled("GPU input: %s", session.gpu_ref.usable ? "yes" : "no");
    const float image_w = ImGui::GetContentRegionAvail().x;
    const float image_h = std::max(80.0f, ImGui::GetContentRegionAvail().y - 92.0f);
    if(session.preview_texture && 0 < session.preview_width && 0 < session.preview_height) {
        const float aspect = (float)session.preview_width / (float)session.preview_height;
        float draw_w = image_w;
        float draw_h = draw_w / aspect;
        if(image_h < draw_h) {
            draw_h = image_h;
            draw_w = draw_h * aspect;
        }
        ImGui::Image((ImTextureID)session.preview_texture, ImVec2(draw_w, draw_h));
    } else {
        ImGui::Dummy(ImVec2(image_w, image_h));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - image_h * 0.55f);
        ImGui::TextDisabled("No preview");
    }
    ImGui::Text("Status: %s", session.status.c_str());
    if(!session.error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", session.error.c_str());
    if(0 < session.connected_info.width && 0 < session.connected_info.height) {
        ImGui::TextDisabled("%ux%u %s frame=%llu fps=%.1f", session.connected_info.width, session.connected_info.height, format_name(session.connected_info.format), (unsigned long long)session.connected_info.frame_counter, session.connected_info.estimated_fps);
    }
    ImGui::EndChild();
    ImGui::PopID();
}

void gui::destroy_session_texture(receiver_session &session) {
    if(session.preview_texture && backend_) backend_->destroy_preview_texture(session.preview_texture);
    session.preview_texture = nullptr;
    session.preview_width = 0;
    session.preview_height = 0;
}

receiver_session *gui::session_for_key(const std::string &key) {
    if(key.empty()) return nullptr;
    auto it = sessions_.find(key);
    return it == sessions_.end() ? nullptr : &it->second;
}

} // namespace nozzle_mixer
