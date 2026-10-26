// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// Built-in GLSL sources. They are compiled with glslang when the renderer first needs a variant.
// (A shipped game can precompile them; the runtime-shader path is the same code with a cache.)
namespace mingol::renderer::shaders {

// Prepended after "#version 450": defines shared by all stages.
extern const char* const kCommon;
extern const char* const kVertex;        // SKINNED optional
extern const char* const kShadowVertex;  // SKINNED optional
extern const char* const kFragment;

}  // namespace mingol::renderer::shaders
