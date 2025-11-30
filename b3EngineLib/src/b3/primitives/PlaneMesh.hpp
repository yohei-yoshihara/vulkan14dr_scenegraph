#ifndef __PRIMITIVES_PLANE_MESH_HPP__
#define __PRIMITIVES_PLANE_MESH_HPP__

#include <memory>

#include "b3/types.hpp"
#include "b3/mesh.hpp"

namespace b3::mesh {

struct PlaneMesh {
  static std::shared_ptr<Mesh> generate(float width, float height, UpAxis up,
                                        int widthSegments, int heightSegments,
                                        UVMap uvMap = {});
};

}

#endif