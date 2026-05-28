# nozzle-mixer design

`nozzle-mixer` is GPU-first. CPU readback is not the mixer implementation path.

## Responsibilities

- `source_registry`: nozzle sender discovery snapshots.
- `mixer_state`: input selection, output size/name, publish toggle, and mode parameters.
- `receiver_session`: one nozzle receiver plus the latest frame lifetime required for GPU texture binding.
- `gpu_compositor`: platform-native render path and output texture ownership.
- `output_publisher`: nozzle sender lifecycle and publishing of the GPU output texture.
- `gui`: ImGui controls, optional diagnostic CPU previews, and orchestration. Diagnostic previews are disabled by default so the mixer path does not read every source back to CPU memory.

## Backend strategy

- macOS uses Metal and publishes an IOSurface-backed output texture through `publish_metal_texture_direct()`.
- Windows uses D3D11 render targets and publishes through `publish_native_texture()`.
- Linux exposes DMA-BUF/EGL input handles, but full output allocation/export requires a dedicated EGL/GBM implementation before runtime publishing is useful.

The API boundary keeps backend-native code out of the app state model. This prevents the project from degenerating into GUI code that happens to call GPU APIs.
