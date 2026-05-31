#include <gpu_compositor.hpp>

#if defined(__APPLE__)
#include <nozzle/backends/metal.hpp>

#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>

#include <string>

namespace nozzle_mixer {

namespace {

static NSString *shader_source() {
    return @R"(
#include <metal_stdlib>
using namespace metal;

struct vertex_out {
    float4 position [[position]];
    float2 uv;
};

vertex vertex_out vertex_main(uint vertex_id [[vertex_id]]) {
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    float2 uvs[3] = { float2(0.0, 1.0), float2(2.0, 1.0), float2(0.0, -1.0) };
    vertex_out out;
    out.position = float4(positions[vertex_id], 0.0, 1.0);
    out.uv = uvs[vertex_id];
    return out;
}

struct params_t {
    uint mode;
    float crossfade;
    float pip_scale;
    float pip_margin_x;
    float pip_margin_y;
    float4 solid_color;
};

fragment float4 fragment_main(vertex_out in [[stage_in]],
                              texture2d<float> input_a [[texture(0)]],
                              texture2d<float> input_b [[texture(1)]],
                              constant params_t &params [[buffer(0)]]) {
    constexpr sampler s(address::clamp_to_edge, filter::linear);
    float2 uv = clamp(in.uv, float2(0.0), float2(1.0));
    bool has_a = input_a.get_width() > 0;
    bool has_b = input_b.get_width() > 0;
    float4 a = has_a ? input_a.sample(s, uv) : params.solid_color;
    float4 b = has_b ? input_b.sample(s, uv) : params.solid_color;
    if(params.mode == 0) return a;
    if(params.mode == 1) return b;
    if(params.mode == 2) return mix(a, b, clamp(params.crossfade, 0.0f, 1.0f));
    if(params.mode == 3) return uv.x < 0.5 ? (has_a ? input_a.sample(s, float2(uv.x * 2.0, uv.y)) : params.solid_color)
                                      : (has_b ? input_b.sample(s, float2((uv.x - 0.5) * 2.0, uv.y)) : params.solid_color);
    if(params.mode == 4) {
        float scale = clamp(params.pip_scale, 0.05f, 1.0f);
        float2 size = float2(scale, scale);
        float2 origin = float2(1.0 - size.x - params.pip_margin_x, 1.0 - size.y - params.pip_margin_y);
        if(all(uv >= origin) && all(uv <= origin + size)) {
            return has_b ? input_b.sample(s, (uv - origin) / size) : a;
        }
        return a;
    }
    return params.solid_color;
}
)";
}

struct metal_params {
    uint32_t mode{0};
    float crossfade{0.0f};
    float pip_scale{0.30f};
    float pip_margin_x{0.04f};
    float pip_margin_y{0.04f};
    float solid_color[4]{0.0f, 0.0f, 0.0f, 1.0f};
};

uint32_t mode_index(mixer_mode mode) {
    switch(mode) {
        case mixer_mode::cut_a: return 0;
        case mixer_mode::cut_b: return 1;
        case mixer_mode::crossfade: return 2;
        case mixer_mode::side_by_side: return 3;
        case mixer_mode::picture_in_picture: return 4;
        case mixer_mode::solid_color: return 5;
    }
    return 5;
}

} // namespace

class metal_gpu_compositor final : public gpu_compositor {
public:
    ~metal_gpu_compositor() override {
        @autoreleasepool {
            [pipeline_ release];
            [queue_ release];
            [output_texture_ release];
            if(surface_) {
                CFRelease(surface_);
                surface_ = nullptr;
            }
            [device_ release];
        }
    }

    bool init(uint32_t width, uint32_t height) override {
        @autoreleasepool {
            device_ = [MTLCreateSystemDefaultDevice() retain];
            if(!device_) return fail("MTLCreateSystemDefaultDevice failed");
            queue_ = [[device_ newCommandQueue] retain];
            if(!queue_) return fail("newCommandQueue failed");
            NSError *error = nil;
            id<MTLLibrary> library = [device_ newLibraryWithSource:shader_source() options:nil error:&error];
            if(!library) return fail(error ? [[error localizedDescription] UTF8String] : "newLibraryWithSource failed");
            id<MTLFunction> vertex = [library newFunctionWithName:@"vertex_main"];
            id<MTLFunction> fragment = [library newFunctionWithName:@"fragment_main"];
            MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
            desc.vertexFunction = vertex;
            desc.fragmentFunction = fragment;
            desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            pipeline_ = [[device_ newRenderPipelineStateWithDescriptor:desc error:&error] retain];
            [desc release];
            [vertex release];
            [fragment release];
            [library release];
            if(!pipeline_) return fail(error ? [[error localizedDescription] UTF8String] : "pipeline creation failed");
            return resize(width, height);
        }
    }

    bool resize(uint32_t width, uint32_t height) override {
        if(width == 0 || height == 0) return fail("invalid output size");
        if(width_ == width && height_ == height && output_texture_) return true;
        @autoreleasepool {
            [output_texture_ release];
            output_texture_ = nil;
            if(surface_) {
                CFRelease(surface_);
                surface_ = nullptr;
            }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            NSDictionary *props = @{
                (NSString *)kIOSurfaceIsGlobal: @(YES),
                (NSString *)kIOSurfaceWidth: @(width),
                (NSString *)kIOSurfaceHeight: @(height),
                (NSString *)kIOSurfaceBytesPerElement: @(4),
                (NSString *)kIOSurfacePixelFormat: @((uint32_t)'BGRA')
            };
#pragma clang diagnostic pop
            surface_ = IOSurfaceCreate((CFDictionaryRef)props);
            if(!surface_) return fail("IOSurfaceCreate failed");
            MTLTextureDescriptor *tex_desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
            tex_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            tex_desc.storageMode = MTLStorageModeShared;
            output_texture_ = [[device_ newTextureWithDescriptor:tex_desc iosurface:surface_ plane:0] retain];
            if(!output_texture_) return fail("IOSurface-backed output texture creation failed");
            width_ = width;
            height_ = height;
            return true;
        }
    }

    bool render(const gpu_frame_ref *input_a, const gpu_frame_ref *input_b, const compositor_params &params) override {
        if(!output_texture_) return fail("output texture is not initialized");
        @autoreleasepool {
            id<MTLCommandBuffer> command_buffer = [queue_ commandBuffer];
            MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = output_texture_;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(params.solid_r, params.solid_g, params.solid_b, params.solid_a);
            id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:pass];
            [encoder setRenderPipelineState:pipeline_];
            id<MTLTexture> texture_a = input_a && input_a->usable ? (__bridge id<MTLTexture>)input_a->native_texture : nil;
            id<MTLTexture> texture_b = input_b && input_b->usable ? (__bridge id<MTLTexture>)input_b->native_texture : nil;
            [encoder setFragmentTexture:texture_a atIndex:0];
            [encoder setFragmentTexture:texture_b atIndex:1];
            metal_params metal{};
            metal.mode = mode_index(params.mode);
            metal.crossfade = clamp_unit(params.crossfade);
            metal.pip_scale = params.pip_scale;
            metal.pip_margin_x = params.pip_margin_x;
            metal.pip_margin_y = params.pip_margin_y;
            metal.solid_color[0] = params.solid_r;
            metal.solid_color[1] = params.solid_g;
            metal.solid_color[2] = params.solid_b;
            metal.solid_color[3] = params.solid_a;
            [encoder setFragmentBytes:&metal length:sizeof(metal) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [encoder endEncoding];
            [command_buffer commit];
            [command_buffer waitUntilCompleted];
            if(command_buffer.status == MTLCommandBufferStatusError) return fail("Metal command buffer failed");
            error_.clear();
            return true;
        }
    }

    bool publish(nozzle::sender &sender) override {
        if(!output_texture_) return fail("no output texture to publish");
        nozzle::metal::direct_publish_desc desc{};
        desc.texture = (__bridge void *)output_texture_;
        desc.width = width_;
        desc.height = height_;
        desc.storage_format = nozzle::texture_format::bgra8_unorm;
        desc.semantic_format = nozzle::texture_format::bgra8_unorm;
        auto result = sender.publish_metal_texture_direct(desc);
        if(!result.ok()) return fail(result.error().message.c_str());
        return true;
    }

    void *preview_texture_handle() const override { return (__bridge void *)output_texture_; }
    const char *backend_name() const override { return "Metal"; }
    const char *last_error() const override { return error_.c_str(); }

private:
    bool fail(const char *message) {
        error_ = message ? message : "unknown Metal compositor error";
        return false;
    }

    id<MTLDevice> device_{nil};
    id<MTLCommandQueue> queue_{nil};
    id<MTLRenderPipelineState> pipeline_{nil};
    id<MTLTexture> output_texture_{nil};
    IOSurfaceRef surface_{nullptr};
    uint32_t width_{0};
    uint32_t height_{0};
    std::string error_{};
};

std::unique_ptr<gpu_compositor> create_gpu_compositor() {
    return std::make_unique<metal_gpu_compositor>();
}

gpu_frame_ref make_gpu_frame_ref(const nozzle::frame &frame) {
    const auto info = frame.info();
    void *texture = nozzle::metal::get_texture(frame.get_texture());
    return gpu_frame_ref{texture, info.width, info.height, info.format, nozzle::backend_type::metal, texture != nullptr};
}

} // namespace nozzle_mixer
#endif
