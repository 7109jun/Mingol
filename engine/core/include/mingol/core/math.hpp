// SPDX-License-Identifier: GPL-2.0-only
// Math types. Conventions: right-handed world space (+Y up, -Z forward), column-major Mat4,
// column vectors (v' = M * v), Vulkan clip space (depth 0..1, +Y down in NDC).
#pragma once

#include <algorithm>
#include <cmath>

namespace mingol {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kEpsilon = 1e-6f;

constexpr float radians(float degrees) noexcept { return degrees * (kPi / 180.0f); }
constexpr float degrees(float radiansValue) noexcept { return radiansValue * (180.0f / kPi); }
constexpr float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }
constexpr float clamp(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

struct Vec2 {
  float x = 0, y = 0;
  constexpr Vec2() = default;
  constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
  friend constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
  friend constexpr Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
  friend constexpr Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
  friend constexpr Vec2 operator*(float s, Vec2 a) { return a * s; }
  friend constexpr bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
};

struct Vec3 {
  float x = 0, y = 0, z = 0;
  constexpr Vec3() = default;
  constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  constexpr Vec3 operator-() const { return {-x, -y, -z}; }
  constexpr Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
  constexpr Vec3& operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  constexpr Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
  friend constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
  friend constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
  friend constexpr Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
  friend constexpr Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
  friend constexpr Vec3 operator*(float s, Vec3 a) { return a * s; }
  friend constexpr Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
  friend constexpr bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
};

struct Vec4 {
  float x = 0, y = 0, z = 0, w = 0;
  constexpr Vec4() = default;
  constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  constexpr Vec4(Vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
  friend constexpr bool operator==(Vec4 a, Vec4 b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
};

constexpr float dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
constexpr float lengthSq(Vec3 v) noexcept { return dot(v, v); }
inline float length(Vec3 v) noexcept { return std::sqrt(lengthSq(v)); }
// Returns the zero vector for (near-)zero input instead of NaNs.
inline Vec3 normalize(Vec3 v) noexcept {
  const float len = length(v);
  return len > kEpsilon ? v / len : Vec3{};
}
constexpr Vec3 lerp(Vec3 a, Vec3 b, float t) noexcept { return a + (b - a) * t; }
inline bool nearlyEqual(float a, float b, float eps = 1e-5f) noexcept { return std::fabs(a - b) <= eps; }
inline bool nearlyEqual(Vec3 a, Vec3 b, float eps = 1e-5f) noexcept {
  return nearlyEqual(a.x, b.x, eps) && nearlyEqual(a.y, b.y, eps) && nearlyEqual(a.z, b.z, eps);
}

struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
  constexpr Quat() = default;
  constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

  static Quat fromAxisAngle(Vec3 axis, float angleRadians) noexcept {
    const Vec3 a = normalize(axis);
    const float h = angleRadians * 0.5f;
    const float s = std::sin(h);
    return {a.x * s, a.y * s, a.z * s, std::cos(h)};
  }
};

constexpr Quat operator*(Quat a, Quat b) noexcept {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
constexpr Quat conjugate(Quat q) noexcept { return {-q.x, -q.y, -q.z, q.w}; }
constexpr float dot(Quat a, Quat b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Quat normalize(Quat q) noexcept {
  const float len = std::sqrt(dot(q, q));
  return len > kEpsilon ? Quat{q.x / len, q.y / len, q.z / len, q.w / len} : Quat{};
}
// Rotates v by unit quaternion q.
inline Vec3 rotate(Quat q, Vec3 v) noexcept {
  const Vec3 u{q.x, q.y, q.z};
  const Vec3 t = 2.0f * cross(u, v);
  return v + q.w * t + cross(u, t);
}
inline Quat slerp(Quat a, Quat b, float t) noexcept {
  float cosTheta = dot(a, b);
  if (cosTheta < 0.0f) {  // take the short path
    b = {-b.x, -b.y, -b.z, -b.w};
    cosTheta = -cosTheta;
  }
  if (cosTheta > 0.9995f) {  // nearly parallel: nlerp
    return normalize(Quat{lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t), lerp(a.w, b.w, t)});
  }
  const float theta = std::acos(cosTheta);
  const float sinTheta = std::sin(theta);
  const float wa = std::sin((1.0f - t) * theta) / sinTheta;
  const float wb = std::sin(t * theta) / sinTheta;
  return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// Column-major: element (row r, column c) is m[c * 4 + r].
struct Mat4 {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  float& at(int row, int col) noexcept { return m[col * 4 + row]; }
  float at(int row, int col) const noexcept { return m[col * 4 + row]; }

  static Mat4 identity() noexcept { return Mat4{}; }

  static Mat4 translation(Vec3 t) noexcept {
    Mat4 r;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
  }
  static Mat4 scale(Vec3 s) noexcept {
    Mat4 r;
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
  }
  static Mat4 rotation(Quat q) noexcept {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat4 r;
    r.m[0] = 1 - 2 * (yy + zz); r.m[1] = 2 * (xy + wz);     r.m[2] = 2 * (xz - wy);
    r.m[4] = 2 * (xy - wz);     r.m[5] = 1 - 2 * (xx + zz); r.m[6] = 2 * (yz + wx);
    r.m[8] = 2 * (xz + wy);     r.m[9] = 2 * (yz - wx);     r.m[10] = 1 - 2 * (xx + yy);
    return r;
  }

  // Right-handed look-at (camera looks down -Z in view space).
  static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 r;
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
    return r;
  }

  // Right-handed perspective, depth range [0,1], Y flipped for Vulkan.
  static Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ) noexcept {
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    Mat4 r;
    for (float& v : r.m) v = 0;
    r.m[0] = f / aspect;
    r.m[5] = -f;
    r.m[10] = farZ / (nearZ - farZ);
    r.m[11] = -1.0f;
    r.m[14] = (nearZ * farZ) / (nearZ - farZ);
    return r;
  }

  // Right-handed orthographic, depth range [0,1], Y flipped for Vulkan.
  static Mat4 orthographic(float left, float right, float bottom, float top, float nearZ, float farZ) noexcept {
    Mat4 r;
    r.m[0] = 2.0f / (right - left);
    r.m[5] = -2.0f / (top - bottom);
    r.m[10] = 1.0f / (nearZ - farZ);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = (top + bottom) / (top - bottom);
    r.m[14] = nearZ / (nearZ - farZ);
    return r;
  }

  static Mat4 trs(Vec3 t, Quat q, Vec3 s) noexcept;
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) noexcept {
  Mat4 r;
  for (int c = 0; c < 4; ++c) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0;
      for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[c * 4 + k];
      r.m[c * 4 + row] = sum;
    }
  }
  return r;
}

inline Mat4 Mat4::trs(Vec3 t, Quat q, Vec3 s) noexcept {
  return translation(t) * rotation(q) * scale(s);
}

inline Vec4 operator*(const Mat4& a, Vec4 v) noexcept {
  return {a.m[0] * v.x + a.m[4] * v.y + a.m[8] * v.z + a.m[12] * v.w,
          a.m[1] * v.x + a.m[5] * v.y + a.m[9] * v.z + a.m[13] * v.w,
          a.m[2] * v.x + a.m[6] * v.y + a.m[10] * v.z + a.m[14] * v.w,
          a.m[3] * v.x + a.m[7] * v.y + a.m[11] * v.z + a.m[15] * v.w};
}

inline Vec3 transformPoint(const Mat4& a, Vec3 p) noexcept {
  const Vec4 r = a * Vec4(p, 1.0f);
  return {r.x, r.y, r.z};
}
inline Vec3 transformDirection(const Mat4& a, Vec3 d) noexcept {
  const Vec4 r = a * Vec4(d, 0.0f);
  return {r.x, r.y, r.z};
}

inline Mat4 transpose(const Mat4& a) noexcept {
  Mat4 r;
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row) r.m[c * 4 + row] = a.m[row * 4 + c];
  return r;
}

// General 4x4 inverse. Returns false (and leaves `out` untouched) if the matrix is singular.
inline bool inverse(const Mat4& in, Mat4& out) noexcept {
  const float* m = in.m;
  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
           m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
           m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
           m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
           m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
           m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
           m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
           m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
           m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
           m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
           m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

  double det = static_cast<double>(m[0]) * inv[0] + static_cast<double>(m[1]) * inv[4] +
               static_cast<double>(m[2]) * inv[8] + static_cast<double>(m[3]) * inv[12];
  if (std::fabs(det) < 1e-12) {
    return false;
  }
  det = 1.0 / det;
  for (int i = 0; i < 16; ++i) out.m[i] = static_cast<float>(inv[i] * det);
  return true;
}

}  // namespace mingol
