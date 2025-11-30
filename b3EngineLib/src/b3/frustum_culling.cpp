#include "frustum_culling.hpp"

#include <iostream>
#include <format>

namespace b3 {

Frustum extractFrustum(const glm::mat4 &vp) {
  Frustum f;

  // 平面 = 行列の行・列の和/差から抽出
  // 行列は m[col][row] ではなく、glm は m[row][col] なので注意

  // Left
  f.planes[0].normal.x = vp[0][3] + vp[0][0];
  f.planes[0].normal.y = vp[1][3] + vp[1][0];
  f.planes[0].normal.z = vp[2][3] + vp[2][0];
  f.planes[0].d = vp[3][3] + vp[3][0];

  // Right
  f.planes[1].normal.x = vp[0][3] - vp[0][0];
  f.planes[1].normal.y = vp[1][3] - vp[1][0];
  f.planes[1].normal.z = vp[2][3] - vp[2][0];
  f.planes[1].d = vp[3][3] - vp[3][0];

  // Bottom
  f.planes[2].normal.x = vp[0][3] + vp[0][1];
  f.planes[2].normal.y = vp[1][3] + vp[1][1];
  f.planes[2].normal.z = vp[2][3] + vp[2][1];
  f.planes[2].d = vp[3][3] + vp[3][1];

  // Top
  f.planes[3].normal.x = vp[0][3] - vp[0][1];
  f.planes[3].normal.y = vp[1][3] - vp[1][1];
  f.planes[3].normal.z = vp[2][3] - vp[2][1];
  f.planes[3].d = vp[3][3] - vp[3][1];

  // Near
  f.planes[4].normal.x = vp[0][3] + vp[0][2];
  f.planes[4].normal.y = vp[1][3] + vp[1][2];
  f.planes[4].normal.z = vp[2][3] + vp[2][2];
  f.planes[4].d = vp[3][3] + vp[3][2];

  // Far
  f.planes[5].normal.x = vp[0][3] - vp[0][2];
  f.planes[5].normal.y = vp[1][3] - vp[1][2];
  f.planes[5].normal.z = vp[2][3] - vp[2][2];
  f.planes[5].d = vp[3][3] - vp[3][2];

  // 正規化（重要）
  for (int i = 0; i < 6; i++) {
    float len = glm::length(f.planes[i].normal);
    f.planes[i].normal /= len;
    f.planes[i].d /= len;
  }

  return f;
}

bool sphereInFrustum(const Frustum &f, const BoundingSphere &boundingSphere) {
  for (int i = 0; i < 6; i++) {
    float dist = f.planes[i].distance(boundingSphere.center);
    if (dist < -boundingSphere.radius) {
      // 完全に外側
      return false;
    }
  }
  return true; // 一部でも中にあれば描画する
}

// Bounding Sphereの計算（Ritter）
// メッシュ頂点: std::vector<glm::vec3> vertices
BoundingSphere computeBoundingSphere(const std::vector<glm::vec3> &v) {
  // ---------- Step1: 最も遠い2点で初期球を作る ----------
  // 適当に最初の点
  glm::vec3 p = v[0];

  // p から最も遠い点 A
  int a = 0;
  float maxDist = 0;
  for (size_t i = 0; i < v.size(); i++) {
    float d = glm::distance2(v[i], p);
    if (d > maxDist) {
      maxDist = d;
      a = i;
    }
  }

  // A から最も遠い点 B
  int b = 0;
  maxDist = 0;
  for (size_t i = 0; i < v.size(); i++) {
    float d = glm::distance2(v[i], v[a]);
    if (d > maxDist) {
      maxDist = d;
      b = i;
    }
  }

  glm::vec3 center = (v[a] + v[b]) * 0.5f;
  float radius = glm::distance(v[a], v[b]) * 0.5f;

  // ---------- Step2: 全頂点で球を拡大 ----------
  for (size_t i = 0; i < v.size(); i++) {
    float d = glm::distance(v[i], center);

    if (d > radius) {
      float newRadius = (radius + d) * 0.5f;
      glm::vec3 dir = glm::normalize(v[i] - center);
      glm::vec3 newCenter = center + dir * (newRadius - radius);

      center = newCenter;
      radius = newRadius;
    }
  }

  return {center, radius};
}

// 頂点構造体からBounding Sphereを計算する。
BoundingSphere computeBoundingSphere(const std::vector<Vertex> &v) {
  // ---------- Step1: 最も遠い2点で初期球を作る ----------
  // 適当に最初の点
  glm::vec3 p = v[0].position;

  // p から最も遠い点 A
  int a = 0;
  float maxDist = 0;
  for (size_t i = 0; i < v.size(); i++) {
    float d = glm::distance2(v[i].position, p);
    if (d > maxDist) {
      maxDist = d;
      a = i;
    }
  }

  // A から最も遠い点 B
  int b = 0;
  maxDist = 0;
  for (size_t i = 0; i < v.size(); i++) {
    float d = glm::distance2(v[i].position, v[a].position);
    if (d > maxDist) {
      maxDist = d;
      b = i;
    }
  }

  glm::vec3 center = (v[a].position + v[b].position) * 0.5f;
  float radius = glm::distance(v[a].position, v[b].position) * 0.5f;

  // ---------- Step2: 全頂点で球を拡大 ----------
  for (size_t i = 0; i < v.size(); i++) {
    float d = glm::distance(v[i].position, center);

    if (d > radius) {
      float newRadius = (radius + d) * 0.5f;
      glm::vec3 dir = glm::normalize(v[i].position - center);
      glm::vec3 newCenter = center + dir * (newRadius - radius);

      center = newCenter;
      radius = newRadius;
    }
  }

  return {center, radius};
}

} // namespace b3
