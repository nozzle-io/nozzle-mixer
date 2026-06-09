# nozzle-mixer

`nozzle-mixer` is a GPU-first desktop mixer for the nozzle texture-sharing ecosystem. It receives live nozzle sources, composites or switches them on the platform GPU, and republishes the result as a new nozzle source.

## Current v0.1 scope

- Discover active nozzle sources.
- Assign sources to Input A and Input B.
- Render mixer output on the GPU.
- Publish a new output source, default name `nozzle-mixer`.
- Modes exposed by the UI:
  - Cut A
  - Cut B
  - Crossfade
  - Side-by-side
  - Picture-in-picture
  - Solid color fallback

The mixer path is intentionally not a CPU readback compositor. CPU pixel readback is disabled by default and is used only when the optional diagnostic preview toggle is enabled; selected mixer inputs still acquire GPU texture handles for the compositor path.

## Backend status

| Platform | GPU compositor | Publish path | Status |
| --- | --- | --- | --- |
| macOS | Metal fragment shader | IOSurface-backed `publish_metal_texture_direct()` | Functional first target |
| Windows | D3D11 render target | `publish_native_texture()` | Buildable GPU output path; shader composition is next hardening step |
| Linux | DMA-BUF/EGL handles | DMA-BUF publish | Build scaffold only; EGL/GBM allocation/export remains explicit follow-up |

This split is deliberate. A fake cross-platform CPU mixer would be easier, but it would not satisfy the latency and bandwidth requirements of a real nozzle mixer.

## Build

```sh
git clone --recursive https://github.com/nozzle-io/nozzle-mixer.git
cd nozzle-mixer
cmake -S . -B build -DNOZZLE_MIXER_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

Linux requires the OpenGL/X11/Wayland packages used by GLFW plus the GBM/DRM/EGL packages used by nozzle.

## Usage

```sh
# macOS bundle build
./build/nozzle-mixer.app/Contents/MacOS/nozzle-mixer

# Linux / Windows non-bundle builds
./build/nozzle-mixer
```

Select Input A and Input B, choose a mixer mode, then enable **Publish output**. The output appears as a new nozzle source using the configured output name.

For deterministic runtime smoke tests, the same binary can run without the GUI and forward one input source through the GPU compositor. On macOS use the bundle executable path:

```sh
./build/nozzle-mixer.app/Contents/MacOS/nozzle-mixer --smoke-forward \
  --source NozzleUnrealSmoke320 \
  --output NozzleMixerSmoke320 \
  --width 320 \
  --height 240 \
  --min-frames 5 \
  --publish-frames 120 \
  --timeout-ms 120000 \
  --hold-ms 5000 \
  --evidence /tmp/nozzle-mixer-smoke.json
```

`--smoke-forward` uses the platform GPU compositor in Cut A mode and republishes via the normal `output_publisher`; it does not use the diagnostic CPU preview/readback path. Treat its PASS as forwarding-path evidence only. For end-to-end runtime evidence, run a downstream receiver such as `nozzle-viewer --smoke-receiver` against the mixer output and verify that the viewer JSON records `sender_name`/`connected_sender_name` matching the mixer output name and `connected_sender_application` as `nozzle-mixer`, with pixels, orientation, channel order, alpha, and moving-frame checks all PASS.

## Limitations

- v0.1 is GPU-first but not yet feature-complete on every backend.
- macOS Metal is the first functional compositor target.
- Windows currently establishes a GPU render-target publish path; full HLSL A/B shader composition is a follow-up.
- Linux requires EGL/GBM DMA-BUF output allocation/export before runtime publishing is useful.
- Strict A/B frame synchronization is not implemented; the mixer uses the latest acquired frame for each input.
- Unsupported formats are listed and can still be diagnosed, but v0.1 focuses on common 8-bit color sources.

## Latest build

The `latest` GitHub release is a replaceable development snapshot from `main`. It publishes macOS, Windows x64, and Linux x64 zip artifacts after CI passes. Version tags (`v*`) publish immutable platform zip artifacts.
