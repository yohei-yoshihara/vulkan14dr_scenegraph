#ifndef __PRIMITIVES_SPHERE_MESH_HPP__
#define __PRIMITIVES_SPHERE_MESH_HPP__

#include <memory>

#include "b3/mesh.hpp"
#include "b3/types.hpp"

namespace b3::mesh {

struct SphereMesh {
  static std::shared_ptr<Mesh> generate(float radius, size_t longs,
                                         size_t lats);
};

} // namespace b3::mesh

#endif