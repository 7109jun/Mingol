// SPDX-License-Identifier: GPL-2.0-only
#include "shaders.hpp"

namespace mingol::renderer::shaders {

// Layout contract with renderer.cpp (FrameData / ObjectData structs). Keep in sync.
const char* const kCommon = R"GLSL(
#define MAX_LIGHTS 8

layout(set = 0, binding = 0, std140) uniform FrameUBO {
  mat4 viewProj;
  mat4 shadowViewProj;
  vec4 cameraPos;
  vec4 ambient;          // rgb ambient, a = 1 when the target is sRGB (hardware encodes)
  vec4 dirDirection;     // xyz direction the light travels, w = 1 when present
  vec4 dirColor;         // rgb colour * intensity, w = 1 when the shadow map is valid
  vec4 shadowParams;     // x normal bias (world), y texel size (uv), z pcf radius (texels)
  ivec4 counts;          // x punctual light count
  vec4 lightPosRange[MAX_LIGHTS];
  vec4 lightColorType[MAX_LIGHTS];  // rgb colour * intensity, w type (1 point, 2 spot)
  vec4 lightDirCos[MAX_LIGHTS];     // xyz direction the light travels, w cos(outer)
  vec4 lightSpot[MAX_LIGHTS];       // x 1 / (cos(inner) - cos(outer))
} frame;

struct ObjectData {
  mat4 model;
  vec4 normalCol0;
  vec4 normalCol1;
  vec4 normalCol2;
  uvec4 skin;  // x: offset of this object's first joint matrix
};

layout(set = 0, binding = 1, std430) readonly buffer Objects { ObjectData objects[]; };
layout(set = 0, binding = 2, std430) readonly buffer SkinMatrices { mat4 skinMats[]; };
#ifndef SHADOW_PASS
layout(set = 0, binding = 3) uniform sampler2DShadow shadowMap;
#endif
)GLSL";

const char* const kVertex = R"GLSL(
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;
#ifdef SKINNED
layout(location = 4) in vec4 inJoints;
layout(location = 5) in vec4 inWeights;
#endif

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec4 vTangent;

void main() {
  ObjectData o = objects[gl_InstanceIndex];
  vec4 pos = vec4(inPosition, 1.0);
  vec3 nrm = inNormal;
  vec3 tan = inTangent.xyz;
#ifdef SKINNED
  uint base = o.skin.x;
  mat4 skin = inWeights.x * skinMats[base + uint(inJoints.x)] +
              inWeights.y * skinMats[base + uint(inJoints.y)] +
              inWeights.z * skinMats[base + uint(inJoints.z)] +
              inWeights.w * skinMats[base + uint(inJoints.w)];
  pos = skin * pos;
  nrm = mat3(skin) * nrm;
  tan = mat3(skin) * tan;
#endif
  vec4 world = o.model * pos;
  mat3 nm = mat3(o.normalCol0.xyz, o.normalCol1.xyz, o.normalCol2.xyz);
  vWorldPos = world.xyz;
  vNormal = nm * nrm;
  vTangent = vec4(mat3(o.model) * tan, inTangent.w);
  vUv = inUv;
  gl_Position = frame.viewProj * world;
}
)GLSL";

const char* const kShadowVertex = R"GLSL(
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;
#ifdef SKINNED
layout(location = 4) in vec4 inJoints;
layout(location = 5) in vec4 inWeights;
#endif

void main() {
  ObjectData o = objects[gl_InstanceIndex];
  vec4 pos = vec4(inPosition, 1.0);
#ifdef SKINNED
  uint base = o.skin.x;
  mat4 skin = inWeights.x * skinMats[base + uint(inJoints.x)] +
              inWeights.y * skinMats[base + uint(inJoints.y)] +
              inWeights.z * skinMats[base + uint(inJoints.z)] +
              inWeights.w * skinMats[base + uint(inJoints.w)];
  pos = skin * pos;
#endif
  gl_Position = frame.shadowViewProj * (o.model * pos);
}
)GLSL";

const char* const kFragment = R"GLSL(
layout(set = 1, binding = 0, std140) uniform MaterialUBO {
  vec4 baseColor;
  vec4 emissive;   // rgb emissive, w normal scale
  vec4 params;     // x metallic, y roughness, z alpha mode (1 = blend)
} material;
layout(set = 1, binding = 1) uniform sampler2D baseColorTex;
layout(set = 1, binding = 2) uniform sampler2D metalRoughTex;
layout(set = 1, binding = 3) uniform sampler2D normalTex;
layout(set = 1, binding = 4) uniform sampler2D emissiveTex;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vTangent;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979;

float distributionGGX(float NdotH, float a) {
  float a2 = a * a;
  float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}
float visibilitySmithGGX(float NdotV, float NdotL, float a) {
  // Height-correlated Smith (already divided by 4 NdotV NdotL).
  float a2 = a * a;
  float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
  float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
  return 0.5 / max(gv + gl, 1e-5);
}
vec3 fresnelSchlick(float VdotH, vec3 f0) {
  return f0 + (1.0 - f0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

float shadowFactor(vec3 worldPos, vec3 N, vec3 L) {
  if (frame.dirColor.w < 0.5) return 1.0;
  float NdotL = clamp(dot(N, L), 0.0, 1.0);
  // Normal-offset: slide the lookup position along the surface normal, more at grazing angles.
  vec3 p = worldPos + N * frame.shadowParams.x * (1.0 - NdotL);
  vec4 sp = frame.shadowViewProj * vec4(p, 1.0);
  vec3 proj = sp.xyz / sp.w;
  vec2 uv = proj.xy * 0.5 + 0.5;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0) return 1.0;
  float r = frame.shadowParams.z;
  float sum = 0.0;
  float count = 0.0;
  for (float y = -r; y <= r; y += 1.0) {
    for (float x = -r; x <= r; x += 1.0) {
      sum += texture(shadowMap, vec3(uv + vec2(x, y) * frame.shadowParams.y, proj.z));
      count += 1.0;
    }
  }
  return sum / count;
}

vec3 evaluateLight(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 diffuseColor, vec3 f0, float a) {
  float NdotL = dot(N, L);
  if (NdotL <= 0.0) return vec3(0.0);
  vec3 H = normalize(V + L);
  float NdotV = max(dot(N, V), 1e-4);
  float NdotH = clamp(dot(N, H), 0.0, 1.0);
  float VdotH = clamp(dot(V, H), 0.0, 1.0);
  vec3 F = fresnelSchlick(VdotH, f0);
  vec3 spec = distributionGGX(NdotH, a) * visibilitySmithGGX(NdotV, NdotL, a) * F;
  vec3 diffuse = (1.0 - F) * diffuseColor / PI;
  return (diffuse + spec) * radiance * NdotL;
}

vec3 acesTonemap(vec3 x) {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
  vec4 base = material.baseColor * texture(baseColorTex, vUv);
  vec3 mr = texture(metalRoughTex, vUv).rgb;
  float metallic = clamp(material.params.x * mr.b, 0.0, 1.0);
  float roughness = clamp(material.params.y * mr.g, 0.045, 1.0);
  float a = roughness * roughness;

  vec3 N = normalize(vNormal);
  if (!gl_FrontFacing) N = -N;
  vec3 T = vTangent.xyz - N * dot(N, vTangent.xyz);
  if (dot(T, T) > 1e-12) {
    T = normalize(T);
    vec3 B = cross(N, T) * vTangent.w;
    vec3 tn = texture(normalTex, vUv).xyz * 2.0 - 1.0;
    tn.xy *= material.emissive.w;
    N = normalize(mat3(T, B, N) * tn);
  }
  vec3 V = normalize(frame.cameraPos.xyz - vWorldPos);

  vec3 f0 = mix(vec3(0.04), base.rgb, metallic);
  vec3 diffuseColor = base.rgb * (1.0 - metallic);

  vec3 Lo = vec3(0.0);
  if (frame.dirDirection.w > 0.5) {
    vec3 L = -normalize(frame.dirDirection.xyz);
    float vis = shadowFactor(vWorldPos, normalize(vNormal), L);
    Lo += evaluateLight(L, frame.dirColor.rgb, N, V, diffuseColor, f0, a) * vis;
  }
  for (int i = 0; i < frame.counts.x; ++i) {
    vec3 toLight = frame.lightPosRange[i].xyz - vWorldPos;
    float dist = length(toLight);
    vec3 L = toLight / max(dist, 1e-4);
    float range = frame.lightPosRange[i].w;
    float window = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
    float atten = window * window / max(dist * dist, 1e-2);
    if (frame.lightColorType[i].w > 1.5) {  // spot
      float cd = dot(-L, normalize(frame.lightDirCos[i].xyz));
      float s = clamp((cd - frame.lightDirCos[i].w) * frame.lightSpot[i].x, 0.0, 1.0);
      atten *= s * s;
    }
    Lo += evaluateLight(L, frame.lightColorType[i].rgb * atten, N, V, diffuseColor, f0, a);
  }

  vec3 color = Lo + frame.ambient.rgb * diffuseColor +
               material.emissive.rgb * texture(emissiveTex, vUv).rgb;
  color = acesTonemap(color);
  if (frame.ambient.a < 0.5) color = pow(color, vec3(1.0 / 2.2));  // UNORM target: encode here
  outColor = vec4(color, material.params.z > 0.5 ? base.a : 1.0);
}
)GLSL";

}  // namespace mingol::renderer::shaders
