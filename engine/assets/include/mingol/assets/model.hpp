// SPDX-License-Identifier: GPL-2.0-only
// glTF 2.0 / GLB loading into plain CPU data. Nothing here touches the GPU; the runtime module
// uploads meshes/textures and instantiates the node tree into a Scene.
//
// Supported: triangle lists/strips/fans, POSITION/NORMAL/TEXCOORD_0/TANGENT/JOINTS_0/WEIGHTS_0,
// metallic-roughness materials (+ KHR_materials_emissive_strength), PNG/JPEG textures, node
// hierarchies, skins, and skeletal TRS animations (LINEAR/STEP).
// Not supported (reported in ModelData::warnings, never silently): alpha MASK (loaded as opaque),
// CUBICSPLINE (tangents dropped, keyframe values used), morph targets, cameras/lights, sparse
// accessors beyond what cgltf resolves, texture transforms, specular-glossiness, a second UV set,
// animation of non-joint nodes, and non-joint ancestors of a skeleton that are themselves animated.
#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "mingol/assets/image.hpp"
#include "mingol/core/error.hpp"
#include "mingol/core/math.hpp"
#include "mingol/renderer/animation.hpp"
#include "mingol/renderer/mesh.hpp"
#include "mingol/renderer/renderer.hpp"

namespace mingol::assets {

struct ModelMaterial {
  std::string name;
  Vec4 baseColor{1, 1, 1, 1};
  float metallic = 1.0F;   // glTF defaults
  float roughness = 1.0F;
  Vec3 emissive{0, 0, 0};
  float normalScale = 1.0F;
  renderer::AlphaMode alphaMode = renderer::AlphaMode::Opaque;
  bool doubleSided = false;
  // Indices into ModelData::images, -1 when absent. Base colour and emissive images are sRGB data;
  // metallic-roughness and normal images are linear data.
  int baseColorImage = -1;
  int metallicRoughnessImage = -1;
  int normalImage = -1;
  int emissiveImage = -1;
};

struct ModelPrimitive {
  renderer::MeshData data;
  int material = -1;  // index into ModelData::materials, -1 = default material
};

// One entry per distinct (glTF mesh, skin) pair that a node uses. Joint indices of skinned
// primitives refer to the reordered skeleton of `skin`.
struct ModelMesh {
  std::string name;
  std::vector<ModelPrimitive> primitives;
  int skin = -1;  // index into ModelData::skins, -1 = static
};

struct ModelNode {
  std::string name;
  int parent = -1;
  Vec3 translation{0, 0, 0};
  Quat rotation{};
  Vec3 scale{1, 1, 1};
  int mesh = -1;  // index into ModelData::meshes
  int skin = -1;  // index into ModelData::skins (the skinned mesh is drawn with an identity node transform)
  std::vector<int> children;
};

struct ModelSkin {
  std::string name;
  renderer::Skeleton skeleton;     // parents precede children
  std::vector<int> jointNodes;     // node index for every skeleton joint
  std::vector<renderer::AnimationClip> clips;  // glTF animations that drive this skeleton
};

struct ModelData {
  std::vector<Image> images;
  std::vector<ModelMaterial> materials;
  std::vector<ModelMesh> meshes;
  std::vector<ModelNode> nodes;
  std::vector<ModelSkin> skins;
  std::vector<int> rootNodes;  // roots of the default scene
  std::vector<std::string> warnings;
};

struct ModelLimits {
  std::uint64_t maxVertices = 1ull << 24;
  std::size_t maxNodes = 1u << 20;
  ImageLimits image;
};

// Resolves an external URI (percent-decoded, relative to the .gltf) to its bytes.
using UriResolver = std::function<Result<std::vector<std::uint8_t>>(const std::string& uri)>;

// `data` is a .glb or a .gltf (JSON). `resolver` is only needed for external .bin/image files;
// data: URIs and the GLB binary chunk are handled internally. All indices, accessor ranges, joint
// indices and sizes are validated; malformed files return an error and never crash.
Result<ModelData> loadGltf(std::span<const std::uint8_t> data, const UriResolver& resolver = {},
                           const ModelLimits& limits = {});

// Convenience: reads the file and resolves external URIs relative to its directory. URIs that
// escape the directory (absolute paths, "..") are rejected.
Result<ModelData> loadGltfFile(const std::filesystem::path& path, const ModelLimits& limits = {});

}  // namespace mingol::assets
