// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/assets/model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include "cgltf.h"
#include "mingol/core/fs.hpp"
#include "mingol/core/math_decompose.hpp"

namespace mingol::assets {
namespace {

struct ResolverCtx {
  const UriResolver* resolver;
  std::string error;
};

cgltf_result fileRead(const cgltf_memory_options*, const cgltf_file_options* opts, const char* path, cgltf_size* size,
                      void** out) {
  auto* ctx = static_cast<ResolverCtx*>(opts->user_data);
  if (ctx == nullptr || ctx->resolver == nullptr || !*ctx->resolver) {
    if (ctx != nullptr) ctx->error = std::string("external resource '") + path + "' needs a resolver";
    return cgltf_result_file_not_found;
  }
  auto bytes = (*ctx->resolver)(path);
  if (!bytes.ok()) {
    ctx->error = std::string("resolving '") + path + "': " + bytes.error().describe();
    return cgltf_result_io_error;
  }
  if (*size != 0 && bytes.value().size() < *size) {
    ctx->error = std::string("resource '") + path + "' is shorter than declared";
    return cgltf_result_data_too_short;
  }
  void* mem = std::malloc(bytes.value().empty() ? 1 : bytes.value().size());
  if (mem == nullptr) return cgltf_result_out_of_memory;
  std::memcpy(mem, bytes.value().data(), bytes.value().size());
  *size = bytes.value().size();
  *out = mem;
  return cgltf_result_success;
}

void fileRelease(const cgltf_memory_options*, const cgltf_file_options*, void* data, cgltf_size) { std::free(data); }

const char* cgltfResultName(cgltf_result r) {
  switch (r) {
    case cgltf_result_success: return "success";
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "unknown format";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_invalid_options: return "invalid options";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "legacy glTF (1.0) is not supported";
    default: return "error";
  }
}

struct DataGuard {
  cgltf_data* d = nullptr;
  ~DataGuard() {
    if (d != nullptr) cgltf_free(d);
  }
};

Mat4 toMat4(const cgltf_float* f) {
  Mat4 m;
  for (int i = 0; i < 16; ++i) m.m[i] = f[i];
  return m;
}

Mat4 worldMatrix(const cgltf_node* n) {
  cgltf_float f[16];
  cgltf_node_transform_world(n, f);
  return toMat4(f);
}

std::string nameOf(const char* n) { return n != nullptr ? n : std::string(); }

class Loader {
 public:
  Loader(const cgltf_data& d, const ModelLimits& l, ModelData& out) : d_(d), limits_(l), out_(out) {}

  Result<void> run() {
    if (d_.nodes_count > limits_.maxNodes) return makeError(ErrorCode::OutOfRange, "{} nodes exceed the limit", d_.nodes_count);
    MINGOL_TRY(checkTree());
    MINGOL_TRY(loadMaterials());
    MINGOL_TRY(loadSkins());
    MINGOL_TRY(loadNodes());
    MINGOL_TRY(loadAnimations());
    // Default scene roots.
    const cgltf_scene* scene = d_.scene != nullptr ? d_.scene : (d_.scenes_count > 0 ? &d_.scenes[0] : nullptr);
    if (scene != nullptr) {
      for (cgltf_size i = 0; i < scene->nodes_count; ++i) out_.rootNodes.push_back(static_cast<int>(nodeIndex(scene->nodes[i])));
    } else {
      for (cgltf_size i = 0; i < d_.nodes_count; ++i) {
        if (d_.nodes[i].parent == nullptr) out_.rootNodes.push_back(static_cast<int>(i));
      }
    }
    return {};
  }

 private:
  const cgltf_data& d_;
  const ModelLimits& limits_;
  ModelData& out_;
  std::map<cgltf_size, int> imageMap_;
  std::map<std::pair<int, int>, int> meshMap_;  // (gltf mesh, skin) -> out_.meshes index
  std::uint64_t vertexTotal_ = 0;
  struct SkinInfo {
    std::vector<int> oldToNew;               // glTF joint position -> skeleton joint index
    std::map<const cgltf_node*, int> byNode;  // node -> skeleton joint index
  };
  std::vector<SkinInfo> skinInfo_;

  void warn(std::string w) {
    if (std::find(out_.warnings.begin(), out_.warnings.end(), w) == out_.warnings.end()) out_.warnings.push_back(std::move(w));
  }
  cgltf_size nodeIndex(const cgltf_node* n) const { return cgltf_node_index(&d_, n); }

  Result<void> checkTree() const {
    for (cgltf_size i = 0; i < d_.nodes_count; ++i) {
      cgltf_size steps = 0;
      for (const cgltf_node* n = d_.nodes[i].parent; n != nullptr; n = n->parent) {
        if (++steps > d_.nodes_count) return makeError(ErrorCode::Corrupt, "node hierarchy contains a cycle");
      }
    }
    return {};
  }

  // ---- images & materials ----------------------------------------------------------------------

  Result<int> imageFor(const cgltf_texture* tex, const char* what) {
    if (tex == nullptr) return -1;
    const cgltf_image* img = tex->image;
    if (img == nullptr) {
      warn(std::string(what) + " texture has no PNG/JPEG image source (e.g. KTX2/WebP/Basis); texture ignored");
      return -1;
    }
    const cgltf_size idx = cgltf_image_index(&d_, img);
    if (auto it = imageMap_.find(idx); it != imageMap_.end()) return it->second;

    std::vector<std::uint8_t> bytes;
    if (img->buffer_view != nullptr) {
      const cgltf_buffer_view* bv = img->buffer_view;
      if (bv->buffer == nullptr || bv->buffer->data == nullptr || bv->offset + bv->size > bv->buffer->size) {
        return makeError(ErrorCode::Corrupt, "image {} buffer view is out of range", idx);
      }
      const auto* p = static_cast<const std::uint8_t*>(bv->buffer->data) + bv->offset;
      bytes.assign(p, p + bv->size);
    } else if (img->uri != nullptr) {
      MINGOL_TRY_ASSIGN(auto b, loadUri(img->uri));
      bytes = std::move(b);
    } else {
      return makeError(ErrorCode::Corrupt, "image {} has neither uri nor bufferView", idx);
    }
    auto decoded = decodeImage(bytes, limits_.image);
    if (!decoded.ok()) return makeError(decoded.error().code, "image {}: {}", idx, decoded.error().message);
    out_.images.push_back(std::move(decoded).value());
    const int outIdx = static_cast<int>(out_.images.size() - 1);
    imageMap_[idx] = outIdx;
    return outIdx;
  }

  Result<std::vector<std::uint8_t>> loadUri(const char* uri) {
    if (std::strncmp(uri, "data:", 5) == 0) {
      const char* comma = std::strchr(uri, ',');
      if (comma == nullptr || comma - uri < 7 || std::strncmp(comma - 7, ";base64", 7) != 0) {
        return makeError(ErrorCode::Unsupported, "only base64 data URIs are supported");
      }
      const std::size_t b64 = std::strlen(comma + 1);
      const std::size_t padding = b64 >= 1 && comma[b64] == '=' ? (b64 >= 2 && comma[b64 - 1] == '=' ? 2 : 1) : 0;
      const std::size_t size = b64 / 4 * 3 - padding;
      cgltf_options opts{};
      void* mem = nullptr;
      if (cgltf_load_buffer_base64(&opts, size, comma + 1, &mem) != cgltf_result_success) {
        return makeError(ErrorCode::Corrupt, "invalid base64 data URI");
      }
      std::vector<std::uint8_t> out(static_cast<std::uint8_t*>(mem), static_cast<std::uint8_t*>(mem) + size);
      std::free(mem);
      return out;
    }
    if (!resolver_ || !*resolver_) return makeError(ErrorCode::NotFound, "external image '{}' needs a resolver", uri);
    std::string decoded = uri;
    decoded.resize(cgltf_decode_uri(decoded.data()));
    return (*resolver_)(decoded);
  }

 public:
  const UriResolver* resolver_ = nullptr;

 private:
  Result<void> loadMaterials() {
    for (cgltf_size i = 0; i < d_.materials_count; ++i) {
      const cgltf_material& m = d_.materials[i];
      ModelMaterial om;
      om.name = nameOf(m.name);
      if (m.has_pbr_metallic_roughness) {
        const auto& p = m.pbr_metallic_roughness;
        om.baseColor = {p.base_color_factor[0], p.base_color_factor[1], p.base_color_factor[2], p.base_color_factor[3]};
        om.metallic = p.metallic_factor;
        om.roughness = p.roughness_factor;
        MINGOL_TRY_ASSIGN(auto a, imageFor(p.base_color_texture.texture, "base colour"));
        om.baseColorImage = a;
        MINGOL_TRY_ASSIGN(auto b, imageFor(p.metallic_roughness_texture.texture, "metallic-roughness"));
        om.metallicRoughnessImage = b;
        if (p.base_color_texture.texcoord != 0 || p.metallic_roughness_texture.texcoord != 0) {
          warn("material '" + om.name + "' uses a second UV set; only TEXCOORD_0 is supported");
        }
        if (p.base_color_texture.has_transform) warn("KHR_texture_transform is not supported; ignored");
      }
      if (m.has_pbr_specular_glossiness) warn("material '" + om.name + "' uses specular-glossiness, which is not supported; base values used");
      om.emissive = {m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]};
      if (m.has_emissive_strength) om.emissive = om.emissive * m.emissive_strength.emissive_strength;
      om.normalScale = m.normal_texture.scale;
      MINGOL_TRY_ASSIGN(auto n, imageFor(m.normal_texture.texture, "normal"));
      om.normalImage = n;
      MINGOL_TRY_ASSIGN(auto e, imageFor(m.emissive_texture.texture, "emissive"));
      om.emissiveImage = e;
      om.doubleSided = m.double_sided != 0;
      switch (m.alpha_mode) {
        case cgltf_alpha_mode_opaque: break;
        case cgltf_alpha_mode_blend: om.alphaMode = renderer::AlphaMode::Blend; break;
        case cgltf_alpha_mode_mask:
          warn("material '" + om.name + "' uses alpha MASK, which the renderer does not support; rendered opaque");
          break;
        default: break;
      }
      for (float f : {om.baseColor.x, om.baseColor.y, om.baseColor.z, om.baseColor.w, om.metallic, om.roughness,
                      om.emissive.x, om.emissive.y, om.emissive.z, om.normalScale}) {
        if (!std::isfinite(f)) return makeError(ErrorCode::Corrupt, "material '{}' has a non-finite factor", om.name);
      }
      out_.materials.push_back(std::move(om));
    }
    return {};
  }

  // ---- skins ------------------------------------------------------------------------------------

  Result<void> loadSkins() {
    skinInfo_.resize(d_.skins_count);
    for (cgltf_size si = 0; si < d_.skins_count; ++si) {
      const cgltf_skin& skin = d_.skins[si];
      const std::size_t n = skin.joints_count;
      if (n == 0) return makeError(ErrorCode::Corrupt, "skin {} has no joints", si);
      if (n > renderer::kMaxJoints) {
        return makeError(ErrorCode::Unsupported, "skin {} has {} joints; the renderer supports {}", si, n, renderer::kMaxJoints);
      }
      std::map<const cgltf_node*, std::size_t> pos;
      for (std::size_t j = 0; j < n; ++j) {
        if (!pos.emplace(skin.joints[j], j).second) return makeError(ErrorCode::Corrupt, "skin {} lists a joint twice", si);
      }
      std::vector<Mat4> ibm(n, Mat4::identity());
      if (skin.inverse_bind_matrices != nullptr) {
        const cgltf_accessor* acc = skin.inverse_bind_matrices;
        if (acc->count < n || acc->type != cgltf_type_mat4) {
          return makeError(ErrorCode::Corrupt, "skin {} inverseBindMatrices does not match its joints", si);
        }
        std::vector<float> f(n * 16);
        if (cgltf_accessor_unpack_floats(acc, f.data(), f.size()) != f.size()) {
          return makeError(ErrorCode::Corrupt, "skin {} inverseBindMatrices unreadable", si);
        }
        for (std::size_t j = 0; j < n; ++j) ibm[j] = toMat4(&f[j * 16]);
      }  // else: identity, as the glTF specification says

      auto jointAncestor = [&](const cgltf_node* node) -> const cgltf_node* {
        for (const cgltf_node* a = node->parent; a != nullptr; a = a->parent) {
          if (pos.count(a) != 0) return a;
        }
        return nullptr;
      };

      // Parents before children.
      std::vector<std::size_t> order;
      std::vector<int> oldToNew(n, -1);
      std::function<void(std::size_t)> place = [&](std::size_t j) {
        if (oldToNew[j] >= 0) return;
        if (const cgltf_node* a = jointAncestor(skin.joints[j])) place(pos[a]);
        oldToNew[j] = static_cast<int>(order.size());
        order.push_back(j);
      };
      for (std::size_t j = 0; j < n; ++j) place(j);

      std::vector<renderer::Joint> joints(n);
      for (std::size_t k = 0; k < n; ++k) {
        const std::size_t j = order[k];
        const cgltf_node* node = skin.joints[j];
        const cgltf_node* anc = jointAncestor(node);
        Mat4 local = worldMatrix(node);
        if (anc != nullptr) {
          Mat4 inv;
          if (!inverse(worldMatrix(anc), inv)) return makeError(ErrorCode::Corrupt, "skin {} has a singular joint transform", si);
          local = inv * local;
        }
        Vec3 t, s;
        Quat r;
        decompose(local, t, r, s);
        joints[k].name = nameOf(node->name).empty() ? "joint" + std::to_string(j) : nameOf(node->name);
        joints[k].parent = anc != nullptr ? oldToNew[pos[anc]] : -1;
        joints[k].bindLocal = {t, r, s};
        joints[k].inverseBind = ibm[j];
      }
      // Joint names must be unique in a Skeleton; glTF does not guarantee that.
      std::set<std::string> seen;
      for (std::size_t k = 0; k < n; ++k) {
        if (!seen.insert(joints[k].name).second) {
          joints[k].name += "#" + std::to_string(order[k]);
          seen.insert(joints[k].name);
        }
      }
      auto sk = renderer::Skeleton::create(std::move(joints));
      if (!sk.ok()) return makeError(sk.error().code, "skin {}: {}", si, sk.error().message);

      ModelSkin ms;
      ms.name = nameOf(skin.name);
      ms.skeleton = std::move(sk).value();
      ms.jointNodes.resize(n);
      SkinInfo& info = skinInfo_[si];
      info.oldToNew = oldToNew;
      for (std::size_t j = 0; j < n; ++j) {
        ms.jointNodes[static_cast<std::size_t>(oldToNew[j])] = static_cast<int>(nodeIndex(skin.joints[j]));
        info.byNode[skin.joints[j]] = oldToNew[j];
      }
      out_.skins.push_back(std::move(ms));
    }
    return {};
  }

  // ---- nodes & meshes -----------------------------------------------------------------------------

  Result<void> loadNodes() {
    out_.nodes.resize(d_.nodes_count);
    for (cgltf_size i = 0; i < d_.nodes_count; ++i) {
      const cgltf_node& n = d_.nodes[i];
      ModelNode& on = out_.nodes[i];
      on.name = nameOf(n.name);
      on.parent = n.parent != nullptr ? static_cast<int>(nodeIndex(n.parent)) : -1;
      cgltf_float f[16];
      cgltf_node_transform_local(&n, f);
      decompose(toMat4(f), on.translation, on.rotation, on.scale);
      for (cgltf_size c = 0; c < n.children_count; ++c) on.children.push_back(static_cast<int>(nodeIndex(n.children[c])));
      if (n.camera != nullptr) warn("node '" + on.name + "' has a camera; cameras are not imported");
      if (n.light != nullptr) warn("node '" + on.name + "' has a light; lights are not imported");
      if (n.mesh != nullptr) {
        const int skinIdx = n.skin != nullptr ? static_cast<int>(cgltf_skin_index(&d_, n.skin)) : -1;
        const int meshIdx = static_cast<int>(cgltf_mesh_index(&d_, n.mesh));
        MINGOL_TRY_ASSIGN(auto mi, meshInstance(meshIdx, skinIdx));
        on.mesh = mi;
        on.skin = skinIdx;
      }
    }
    return {};
  }

  Result<int> meshInstance(int meshIdx, int skinIdx) {
    const auto key = std::pair{meshIdx, skinIdx};
    if (auto it = meshMap_.find(key); it != meshMap_.end()) return it->second;
    const cgltf_mesh& m = d_.meshes[meshIdx];
    ModelMesh om;
    om.name = nameOf(m.name);
    om.skin = skinIdx;
    for (cgltf_size p = 0; p < m.primitives_count; ++p) {
      MINGOL_TRY_ASSIGN(auto prim, loadPrimitive(m.primitives[p], skinIdx, om.name));
      if (!prim.data.vertices.empty()) om.primitives.push_back(std::move(prim));
    }
    out_.meshes.push_back(std::move(om));
    const int idx = static_cast<int>(out_.meshes.size() - 1);
    meshMap_[key] = idx;
    return idx;
  }

  Result<std::vector<float>> readAttr(const cgltf_accessor* a, cgltf_type type, std::size_t count, const char* name) {
    if (a->type != type || a->count != count) {
      return makeError(ErrorCode::Corrupt, "attribute {} has the wrong type or count", name);
    }
    const std::size_t comps = cgltf_num_components(type);
    std::vector<float> v(count * comps);
    if (cgltf_accessor_unpack_floats(a, v.data(), v.size()) != v.size()) {
      return makeError(ErrorCode::Corrupt, "attribute {} could not be read", name);
    }
    for (float f : v) {
      if (!std::isfinite(f)) return makeError(ErrorCode::Corrupt, "attribute {} contains NaN/Inf", name);
    }
    return v;
  }

  Result<ModelPrimitive> loadPrimitive(const cgltf_primitive& prim, int skinIdx, const std::string& meshName) {
    ModelPrimitive out;
    if (prim.type != cgltf_primitive_type_triangles && prim.type != cgltf_primitive_type_triangle_strip &&
        prim.type != cgltf_primitive_type_triangle_fan) {
      warn("mesh '" + meshName + "' has a non-triangle primitive (points/lines); skipped");
      return out;
    }
    if (prim.targets_count > 0) warn("mesh '" + meshName + "' has morph targets, which are not supported; base shape used");

    const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *tan = nullptr, *jnt = nullptr, *wgt = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
      const cgltf_attribute& at = prim.attributes[i];
      if (at.type == cgltf_attribute_type_position && at.index == 0) pos = at.data;
      else if (at.type == cgltf_attribute_type_normal && at.index == 0) nrm = at.data;
      else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
      else if (at.type == cgltf_attribute_type_tangent && at.index == 0) tan = at.data;
      else if (at.type == cgltf_attribute_type_joints && at.index == 0) jnt = at.data;
      else if (at.type == cgltf_attribute_type_weights && at.index == 0) wgt = at.data;
      else if (at.type == cgltf_attribute_type_joints || at.type == cgltf_attribute_type_weights) {
        warn("mesh '" + meshName + "' has more than 4 joint influences per vertex; extra influences are dropped");
      }
    }
    if (pos == nullptr) return makeError(ErrorCode::Corrupt, "mesh '{}' primitive has no POSITION", meshName);
    const std::size_t vc = pos->count;
    if (vc == 0) return out;
    vertexTotal_ += vc;
    if (vertexTotal_ > limits_.maxVertices) return makeError(ErrorCode::OutOfRange, "model exceeds {} vertices", limits_.maxVertices);

    MINGOL_TRY_ASSIGN(auto P, readAttr(pos, cgltf_type_vec3, vc, "POSITION"));
    std::vector<float> N, T, UV;
    if (nrm != nullptr) { MINGOL_TRY_ASSIGN(auto v, readAttr(nrm, cgltf_type_vec3, vc, "NORMAL")); N = std::move(v); }
    if (uv != nullptr) { MINGOL_TRY_ASSIGN(auto v, readAttr(uv, cgltf_type_vec2, vc, "TEXCOORD_0")); UV = std::move(v); }
    if (tan != nullptr) { MINGOL_TRY_ASSIGN(auto v, readAttr(tan, cgltf_type_vec4, vc, "TANGENT")); T = std::move(v); }

    // Indices -> triangle list.
    std::vector<std::uint32_t> raw;
    if (prim.indices != nullptr) {
      raw.resize(prim.indices->count);
      if (cgltf_accessor_unpack_indices(prim.indices, raw.data(), 4, raw.size()) != raw.size()) {
        return makeError(ErrorCode::Corrupt, "mesh '{}' indices could not be read", meshName);
      }
    } else {
      raw.resize(vc);
      for (std::size_t i = 0; i < vc; ++i) raw[i] = static_cast<std::uint32_t>(i);
    }
    for (std::uint32_t i : raw) {
      if (i >= vc) return makeError(ErrorCode::Corrupt, "mesh '{}' has an index ({}) beyond its {} vertices", meshName, i, vc);
    }
    std::vector<std::uint32_t>& idx = out.data.indices;
    if (prim.type == cgltf_primitive_type_triangles) {
      if (raw.size() % 3 != 0) return makeError(ErrorCode::Corrupt, "mesh '{}' index count is not a multiple of 3", meshName);
      idx = std::move(raw);
    } else if (prim.type == cgltf_primitive_type_triangle_strip) {
      for (std::size_t i = 2; i < raw.size(); ++i) {
        if (i % 2 == 0) idx.insert(idx.end(), {raw[i - 2], raw[i - 1], raw[i]});
        else idx.insert(idx.end(), {raw[i - 1], raw[i - 2], raw[i]});
      }
    } else {
      for (std::size_t i = 2; i < raw.size(); ++i) idx.insert(idx.end(), {raw[0], raw[i - 1], raw[i]});
    }
    if (idx.empty()) return makeError(ErrorCode::Corrupt, "mesh '{}' primitive has no triangles", meshName);

    auto& verts = out.data.vertices;
    verts.resize(vc);
    for (std::size_t i = 0; i < vc; ++i) {
      verts[i].position = {P[i * 3], P[i * 3 + 1], P[i * 3 + 2]};
      if (!N.empty()) verts[i].normal = normalize(Vec3{N[i * 3], N[i * 3 + 1], N[i * 3 + 2]});
      if (!UV.empty()) verts[i].uv = {UV[i * 2], UV[i * 2 + 1]};
      if (!T.empty()) verts[i].tangent = {T[i * 4], T[i * 4 + 1], T[i * 4 + 2], T[i * 4 + 3] < 0 ? -1.0F : 1.0F};
    }
    if (N.empty()) {
      warn("mesh '" + meshName + "' has no normals; smooth normals were generated");
      generateNormals(out.data);
    }
    if (T.empty()) generateTangents(out.data, !UV.empty());

    if (skinIdx >= 0) {
      if (jnt == nullptr || wgt == nullptr) return makeError(ErrorCode::Corrupt, "mesh '{}' is skinned but has no JOINTS_0/WEIGHTS_0", meshName);
      MINGOL_TRY_ASSIGN(auto J, readAttr(jnt, cgltf_type_vec4, vc, "JOINTS_0"));
      MINGOL_TRY_ASSIGN(auto W, readAttr(wgt, cgltf_type_vec4, vc, "WEIGHTS_0"));
      const auto& remap = skinInfo_[static_cast<std::size_t>(skinIdx)].oldToNew;
      out.data.skin.resize(vc);
      for (std::size_t i = 0; i < vc; ++i) {
        float sum = 0;
        for (int k = 0; k < 4; ++k) sum += std::max(0.0F, W[i * 4 + k]);
        renderer::SkinVertex sv;
        for (int k = 0; k < 4; ++k) {
          const float w = std::max(0.0F, W[i * 4 + k]);
          const float jf = J[i * 4 + k];
          if (w > 0.0F) {
            if (jf < 0 || jf >= static_cast<float>(remap.size())) {
              return makeError(ErrorCode::Corrupt, "mesh '{}' vertex {} references joint {} of {}", meshName, i, jf, remap.size());
            }
            sv.joints[k] = static_cast<float>(remap[static_cast<std::size_t>(jf)]);
          } else {
            sv.joints[k] = 0;
          }
          sv.weights[k] = sum > 1e-6F ? w / sum : (k == 0 ? 1.0F : 0.0F);
        }
        out.data.skin[i] = sv;
      }
    } else if (jnt != nullptr) {
      warn("mesh '" + meshName + "' has joint data but its node has no skin; drawn static");
    }
    out.material = prim.material != nullptr ? static_cast<int>(cgltf_material_index(&d_, prim.material)) : -1;
    if (auto v = out.data.validate(); !v.ok()) return makeError(v.error().code, "mesh '{}': {}", meshName, v.error().message);
    return out;
  }

  static void generateNormals(renderer::MeshData& m) {
    std::vector<Vec3> acc(m.vertices.size(), Vec3{});
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
      const auto a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
      const Vec3 n = cross(m.vertices[b].position - m.vertices[a].position, m.vertices[c].position - m.vertices[a].position);
      acc[a] = acc[a] + n;  // area weighted: the cross product's length is twice the area
      acc[b] = acc[b] + n;
      acc[c] = acc[c] + n;
    }
    for (std::size_t i = 0; i < acc.size(); ++i) {
      m.vertices[i].normal = lengthSq(acc[i]) > 1e-20F ? normalize(acc[i]) : Vec3{0, 1, 0};
    }
  }

  static void generateTangents(renderer::MeshData& m, bool haveUv) {
    const std::size_t n = m.vertices.size();
    std::vector<Vec3> tan(n, Vec3{}), bit(n, Vec3{});
    if (haveUv) {
      for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const auto a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
        const Vec3 e1 = m.vertices[b].position - m.vertices[a].position;
        const Vec3 e2 = m.vertices[c].position - m.vertices[a].position;
        const float du1 = m.vertices[b].uv.x - m.vertices[a].uv.x, dv1 = m.vertices[b].uv.y - m.vertices[a].uv.y;
        const float du2 = m.vertices[c].uv.x - m.vertices[a].uv.x, dv2 = m.vertices[c].uv.y - m.vertices[a].uv.y;
        const float det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12F) continue;
        const float r = 1.0F / det;
        const Vec3 t = (e1 * dv2 - e2 * dv1) * r;
        const Vec3 bt = (e2 * du1 - e1 * du2) * r;
        for (auto v : {a, b, c}) {
          tan[v] = tan[v] + t;
          bit[v] = bit[v] + bt;
        }
      }
    }
    for (std::size_t i = 0; i < n; ++i) {
      const Vec3 nrm = m.vertices[i].normal;
      Vec3 t = tan[i] - nrm * dot(nrm, tan[i]);  // Gram-Schmidt
      if (lengthSq(t) < 1e-20F) {
        // No usable UV derivative: any vector perpendicular to the normal.
        const Vec3 axis = std::fabs(nrm.y) < 0.9F ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        t = cross(axis, nrm);
      }
      t = normalize(t);
      const float w = dot(cross(nrm, t), bit[i]) < 0.0F ? -1.0F : 1.0F;
      m.vertices[i].tangent = {t.x, t.y, t.z, w};
    }
  }

  // ---- animations -----------------------------------------------------------------------------------

  Result<void> loadAnimations() {
    for (cgltf_size ai = 0; ai < d_.animations_count; ++ai) {
      const cgltf_animation& anim = d_.animations[ai];
      const std::string aname = nameOf(anim.name).empty() ? "animation" + std::to_string(ai) : nameOf(anim.name);
      for (std::size_t si = 0; si < out_.skins.size(); ++si) {
        std::map<std::size_t, renderer::Channel> byJoint;
        float duration = 0.0F;
        for (cgltf_size ci = 0; ci < anim.channels_count; ++ci) {
          const cgltf_animation_channel& ch = anim.channels[ci];
          if (ch.target_node == nullptr) continue;
          const auto it = skinInfo_[si].byNode.find(ch.target_node);
          if (it == skinInfo_[si].byNode.end()) continue;
          if (ch.target_path == cgltf_animation_path_type_weights) {
            warn("animation '" + aname + "' animates morph weights, which are not supported");
            continue;
          }
          const cgltf_animation_sampler* s = ch.sampler;
          if (s == nullptr || s->input == nullptr || s->output == nullptr) return makeError(ErrorCode::Corrupt, "animation '{}' has an incomplete sampler", aname);
          const bool cubic = s->interpolation == cgltf_interpolation_type_cubic_spline;
          if (cubic) warn("animation '" + aname + "' uses CUBICSPLINE; tangents are dropped and keyframe values used");
          const std::size_t n = s->input->count;
          const bool rot = ch.target_path == cgltf_animation_path_type_rotation;
          const std::size_t comps = rot ? 4 : 3;
          if (s->output->count != (cubic ? n * 3 : n) || s->input->type != cgltf_type_scalar ||
              s->output->type != (rot ? cgltf_type_vec4 : cgltf_type_vec3)) {
            return makeError(ErrorCode::Corrupt, "animation '{}' sampler accessors do not match", aname);
          }
          std::vector<float> times(n), vals(s->output->count * comps);
          if (cgltf_accessor_unpack_floats(s->input, times.data(), n) != n ||
              cgltf_accessor_unpack_floats(s->output, vals.data(), vals.size()) != vals.size()) {
            return makeError(ErrorCode::Corrupt, "animation '{}' data unreadable", aname);
          }
          for (float v : vals) if (!std::isfinite(v)) return makeError(ErrorCode::Corrupt, "animation '{}' contains NaN/Inf", aname);
          renderer::Channel& rc = byJoint[static_cast<std::size_t>(it->second)];
          rc.joint = static_cast<std::size_t>(it->second);
          const auto interp = s->interpolation == cgltf_interpolation_type_step ? renderer::Interpolation::Step : renderer::Interpolation::Linear;
          auto valueAt = [&](std::size_t k) { return cubic ? k * 3 + 1 : k; };
          if (n > 0) duration = std::max(duration, times[n - 1]);
          if (ch.target_path == cgltf_animation_path_type_rotation) {
            rc.rotation.times = times;
            rc.rotation.interpolation = interp;
            for (std::size_t k = 0; k < n; ++k) {
              const float* q = &vals[valueAt(k) * 4];
              rc.rotation.values.push_back(normalize(Quat{q[0], q[1], q[2], q[3]}));
            }
          } else {
            auto& track = ch.target_path == cgltf_animation_path_type_translation ? rc.translation : rc.scale;
            if (ch.target_path != cgltf_animation_path_type_translation && ch.target_path != cgltf_animation_path_type_scale) continue;
            track.times = times;
            track.interpolation = interp;
            for (std::size_t k = 0; k < n; ++k) {
              const float* v = &vals[valueAt(k) * 3];
              track.values.push_back({v[0], v[1], v[2]});
            }
          }
        }
        if (byJoint.empty()) continue;
        if (!(duration > 0.0F)) {
          warn("animation '" + aname + "' has zero duration; skipped");
          continue;
        }
        std::vector<renderer::Channel> channels;
        for (auto& [j, c] : byJoint) channels.push_back(std::move(c));
        auto clip = renderer::AnimationClip::create(aname, duration, std::move(channels));
        if (!clip.ok()) return makeError(clip.error().code, "animation '{}': {}", aname, clip.error().message);
        out_.skins[si].clips.push_back(std::move(clip).value());
      }
      // Channels that target nodes outside every skin.
      for (cgltf_size ci = 0; ci < anim.channels_count; ++ci) {
        const cgltf_node* t = anim.channels[ci].target_node;
        bool inSkin = false;
        for (const auto& info : skinInfo_) inSkin = inSkin || (t != nullptr && info.byNode.count(t) != 0);
        if (t != nullptr && !inSkin && anim.channels[ci].target_path != cgltf_animation_path_type_weights) {
          warn("animation '" + aname + "' animates node '" + nameOf(t->name) + "' which is not a skeleton joint; ignored");
          break;
        }
      }
    }
    return {};
  }
};

}  // namespace

Result<ModelData> loadGltf(std::span<const std::uint8_t> data, const UriResolver& resolver, const ModelLimits& limits) {
  if (data.empty()) return makeError(ErrorCode::InvalidArgument, "empty glTF data");
  ResolverCtx ctx{&resolver, {}};
  cgltf_options opts{};
  opts.file.read = &fileRead;
  opts.file.release = &fileRelease;
  opts.file.user_data = &ctx;

  DataGuard guard;
  if (auto r = cgltf_parse(&opts, data.data(), data.size(), &guard.d); r != cgltf_result_success) {
    return makeError(r == cgltf_result_legacy_gltf ? ErrorCode::Unsupported : ErrorCode::Corrupt, "glTF parse failed: {}", cgltfResultName(r));
  }
  if (auto r = cgltf_load_buffers(&opts, guard.d, ""); r != cgltf_result_success) {
    return makeError(r == cgltf_result_file_not_found || r == cgltf_result_io_error ? ErrorCode::NotFound : ErrorCode::Corrupt,
                     "glTF buffers: {}{}{}", cgltfResultName(r), ctx.error.empty() ? "" : " - ", ctx.error);
  }
  if (auto r = cgltf_validate(guard.d); r != cgltf_result_success) {
    return makeError(ErrorCode::Corrupt, "glTF validation failed: {}", cgltfResultName(r));
  }
  ModelData out;
  Loader loader(*guard.d, limits, out);
  loader.resolver_ = &resolver;
  MINGOL_TRY(loader.run());
  return out;
}

Result<ModelData> loadGltfFile(const std::filesystem::path& path, const ModelLimits& limits) {
  MINGOL_TRY_ASSIGN(auto bytes, fs::readFile(path));
  const std::filesystem::path dir = path.parent_path();
  UriResolver resolver = [dir](const std::string& uri) -> Result<std::vector<std::uint8_t>> {
    const std::filesystem::path rel = std::filesystem::path(std::u8string(uri.begin(), uri.end()));
    if (rel.is_absolute() || rel.has_root_name() || uri.find("://") != std::string::npos) {
      return makeError(ErrorCode::InvalidArgument, "absolute or remote URI '{}' is not allowed", uri);
    }
    for (const auto& part : rel) {
      if (part == "..") return makeError(ErrorCode::InvalidArgument, "URI '{}' escapes the model directory", uri);
    }
    return fs::readFile(dir / rel);
  };
  return loadGltf(bytes, resolver, limits);
}

}  // namespace mingol::assets
