#ifndef __PRIMITIVES_CUBE_MESH_HPP__
#define __PRIMITIVES_CUBE_MESH_HPP__

#include <memory>

#include "b3/mesh.hpp"
#include "b3/types.hpp"

namespace b3::mesh {

struct CubeMesh {
  static std::shared_ptr<Mesh> generate(float width, float height, float depth, size_t nx, size_t ny);
};

} // namespace b3::mesh

#endif