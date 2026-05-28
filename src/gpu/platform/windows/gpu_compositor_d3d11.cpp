#include <gpu_compositor.hpp>

#if defined(_WIN32)
#include <nozzle/backends/d3d11.hpp>

#include <d3d11.h>
#include <wrl/client.h>

#include <string>

namespace nozzle_mixer {

class d3d11_gpu_compositor final : public gpu_compositor {
public:
    bool init(uint32_t width, uint32_t height) override {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL selected{};
        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            levels,
            2,
            D3D11_SDK_VERSION,
            device_.GetAddressOf(),
            &selected,
            context_.GetAddressOf());
        if(FAILED(hr)) return fail("D3D11CreateDevice failed");
        return resize(width, height);
    }

    bool resize(uint32_t width, uint32_t height) override {
        if(width == 0 || height == 0) return fail("invalid output size");
        if(width_ == width && height_ == height && output_) return true;
        output_.Reset();
        render_target_.Reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        HRESULT hr = device_->CreateTexture2D(&desc, nullptr, output_.GetAddressOf());
        if(FAILED(hr)) return fail("CreateTexture2D output failed");
        hr = device_->CreateRenderTargetView(output_.Get(), nullptr, render_target_.GetAddressOf());
        if(FAILED(hr)) return fail("CreateRenderTargetView output failed");
        width_ = width;
        height_ = height;
        return true;
    }

    bool render(const gpu_frame_ref *input_a, const gpu_frame_ref *input_b, const compositor_params &params) override {
        (void)input_a;
        (void)input_b;
        if(!render_target_) return fail("render target is not initialized");
        const float color[4] = {params.solid_r, params.solid_g, params.solid_b, params.solid_a};
        context_->ClearRenderTargetView(render_target_.Get(), color);
        context_->Flush();
        error_.clear();
        return true;
    }

    bool publish(nozzle::sender &sender) override {
        if(!output_) return fail("no D3D11 output texture to publish");
        auto result = sender.publish_native_texture(output_.Get(), width_, height_, nozzle::texture_format::bgra8_unorm);
        if(!result.ok()) return fail(result.error().message.c_str());
        return true;
    }

    void *preview_texture_handle() const override { return output_.Get(); }
    const char *backend_name() const override { return "D3D11"; }
    const char *last_error() const override { return error_.c_str(); }

private:
    bool fail(const char *message) {
        error_ = message ? message : "unknown D3D11 compositor error";
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device_{};
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output_{};
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_{};
    uint32_t width_{0};
    uint32_t height_{0};
    std::string error_{};
};

std::unique_ptr<gpu_compositor> create_gpu_compositor() {
    return std::make_unique<d3d11_gpu_compositor>();
}

gpu_frame_ref make_gpu_frame_ref(const nozzle::frame &frame) {
    const auto info = frame.info();
    ID3D11Texture2D *texture = nozzle::d3d11::get_texture(frame.get_texture());
    return gpu_frame_ref{texture, info.width, info.height, info.format, nozzle::backend_type::d3d11, texture != nullptr};
}

} // namespace nozzle_mixer
#endif
