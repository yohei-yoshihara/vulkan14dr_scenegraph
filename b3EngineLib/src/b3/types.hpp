#ifndef __TYPES_HPP__
#define __TYPES_HPP__

#include "common.hpp"

namespace b3 {

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 texCoord;
};

using IndexType = uint32_t;

enum class UpAxis { X, Y, Z };

struct RGBAColor {
  float r, g, b, a;
};

// VulkanとOpenGLでは、テクスチャ座標系が上下逆転している点に注意
//
// Vulkan:
//   A(0,0) -- B(1,0)
//      |
//      |
//   C(0,1)
//
// OpenGL:
//   C(0,1)
//      |
//      |
//   A(0,0) -- B(1,0)
//
struct UVMap {
  // 左上の座標
  glm::vec2 a = {0.0f, 0.0f};

  // 右上の座標
  glm::vec2 b = {1.0f, 0.0f};

  // 左下の座標
  glm::vec2 c = {0.0f, 1.0f};
};

} // namespace b3

#endif