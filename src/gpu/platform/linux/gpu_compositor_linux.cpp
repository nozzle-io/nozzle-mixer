#include <gpu_compositor.hpp>

#if defined(__linux__)
#include <nozzle/backends/linux.hpp>

#include <string>

namespace nozzle_mixer {

class linux_gpu_compositor final : public gpu_compositor {
public:
    bool init(uint32_t width, uint32_t height) override {
        width_ = width;
        height_ = height;
        error_ = "Linux DMA-BUF GPU compositor needs EGL/GBM output allocation; build scaffold is present but runtime publishing is disabled";
        return 0 < width && 0 < height;
    }

    bool resize(uint32_t width, uint32_t height) override {
        if(width == 0 || height == 0) return fail("invalid output size");
        width_ = width;
        height_ = height;
        return true;
    }

    bool render(const gpu_frame_ref *input_a, const gpu_frame_ref *input_b, const compositor_params &params) override {
        (void)input_a;
        (void)input_b;
        (void)params;
        return fail("Linux GPU render path requires EGL/GBM compositor implementation");
    }

    bool publish(nozzle::sender &sender) override {
        (void)sender;
        return fail("Linux GPU publish path requires DMA-BUF export implementation");
    }

    void *preview_texture_handle() const override { return nullptr; }
    const char *backend_name() const override { return "DMA-BUF/EGL"; }
    const char *last_error() const override { return error_.c_str(); }

private:
    bool fail(const char *message) {
        error_ = message ? message : "unknown Linux compositor error";
        return false;
    }

    uint32_t width_{0};
    uint32_t height_{0};
    std::string error_{};
};

std::unique_ptr<gpu_compositor> create_gpu_compositor() {
    return std::make_unique<linux_gpu_compositor>();
}

gpu_frame_ref make_gpu_frame_ref(const nozzle::frame &frame) {
    const auto info = frame.info();
    void *egl_image = nozzle::dma_buf::get_egl_image(frame.get_texture());
    return gpu_frame_ref{egl_image, info.width, info.height, info.format, nozzle::backend_type::dma_buf, egl_image != nullptr};
}

} // namespace nozzle_mixer
#endif
