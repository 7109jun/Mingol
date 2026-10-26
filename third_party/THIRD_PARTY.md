# Third-party components

All are compatible with distributing Mingol under GPL-2.0-only (permissive licences, no
copyleft conflict, no patent clauses incompatible with GPLv2). Their licence texts are kept next
to the code in `third_party/<name>/LICENSE`.

| Component | Version / commit | Licence | Used for | Notes |
|---|---|---|---|---|
| Vulkan Memory Allocator | see `third_party/vma` | MIT | GPU memory | vendored |
| stb_image | v2.30 (nothings/stb a7d4c21) | MIT or public domain | PNG/JPEG decoding | vendored; only PNG+JPEG compiled in; allocations zero-initialised (see `engine/assets/src/third_party_impl.cpp`) |
| stb_vorbis | 1.22 (nothings/stb a7d4c21) | MIT or public domain | Ogg Vorbis decoding | vendored; used with a fixed arena (it leaks temporaries on corrupt headers otherwise) |
| cgltf | 1.14+ (jkuhlmann/cgltf 85cd623) | MIT | glTF 2.0 parsing | vendored |
| miniaudio | (mackron/miniaudio 9634bed) | MIT-0 or public domain | audio output/mixing backend | vendored |
| Bullet Physics | 3.24 (system package `libbullet-dev`) | zlib | rigid-body physics | linked, not vendored; Windows builds need it from vcpkg |
| glslang / SPIRV-Tools | system packages | BSD-3 / Apache-2.0 | shader compilation | linked, not vendored |
| GLFW | 3.3 (system package) | zlib | windowing | linked, not vendored |

The vendored single-header libraries are unmodified upstream files.
