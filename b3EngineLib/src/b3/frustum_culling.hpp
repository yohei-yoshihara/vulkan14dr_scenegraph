#ifndef __FRUSTUM_CULLING_HPP__
#define __FRUSTUM_CULLING_HPP__

#include "common.hpp"
#include "types.hpp"
#include <array>

namespace b3 {

// 平面
struct Plane {
  // 法線
  glm::vec3 normal;
  // 原点からの距離
  float d; // ax + by + cz + d = 0

  // 点と平面の距離
  float distance(const glm::vec3 &p) const { return glm::dot(normal, p) + d; }
};

struct Frustum {
  // 0: Left, 1: Right, 2: Bottom, 3: Top, 4: Near, 5: Far
  std::array<Plane, 6> planes; 
};

// ViewProjection 行列から6つの平面を抽出
// Shadow Mapを描画する際は、Shadow Map描画時のVPマトリックスを用いること。
// そうすることで、シーンに描画されないが、影は落とすオブジェクトの影も描画される。
Frustum extractFrustum(const glm::mat4 &vp);

// Bounding Sphere
struct BoundingSphere {
  // 中心
  glm::vec3 center = {0.f, 0.f, 0.f};
  // 半径
  float radius = 0.f;
};

// Bounding SphereがFrustum内かどうかを判定する。
// 少しでも重なっていればtrueが返る。
bool sphereInFrustum(const Frustum &f, const BoundingSphere &boundingSphere);

// 頂点の配列からBounding Sphereを計算する。
BoundingSphere computeBoundingSphere(const std::vector<glm::vec3> &v);

// 頂点構造体からBounding Sphereを計算する。
BoundingSphere computeBoundingSphere(const std::vector<Vertex> &v);

} // namespace b3

#endif