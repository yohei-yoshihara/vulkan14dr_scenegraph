#include "SphereMesh.hpp"

#include <cmath>
#include <numbers>

namespace b3::mesh {

std::shared_ptr<Mesh> SphereMesh::generate(float radius, size_t longs, size_t lats) {
  constexpr auto pi = std::numbers::pi_v<float>;
  auto mesh =
      std::make_shared<Mesh>();
  for (size_t latNumber = 0; latNumber <= lats; ++latNumber) {
    for (size_t longNumber = 0; longNumber <= longs; ++longNumber) {
      float theta = latNumber * pi / lats;
      float phi = longNumber * 2 * pi / longs;

      float sinTheta = std::sin(theta);
      float sinPhi = std::sin(phi);
      float cosTheta = std::cos(theta);
      float cosPhi = std::cos(phi);

      float x = cosPhi * sinTheta;
      float y = cosTheta;
      float z = sinPhi * sinTheta;
      float u = 1.0f - (1.0f * longNumber / longs);
      float v = 1.0f * latNumber / lats;

      Vertex vertex;
      vertex.position = {
        radius * x,
        radius * y,
        radius * z
      };

      vertex.normal = {
        x, y, z
      };

      vertex.texCoord = {u, v};

      mesh->addVertex(vertex);
    }
  }

  for (size_t latNumber = 0; latNumber < lats; ++latNumber) {
    for (size_t longNumber = 0; longNumber < longs; ++longNumber) {

      int first = (latNumber * (longs + 1)) + longNumber;
      int second = first + (longs + 1);
      int third = first + 1;
      int fourth = second + 1;

      mesh->addIndex(first);
      mesh->addIndex(third);
      mesh->addIndex(second);

      mesh->addIndex(second);
      mesh->addIndex(third);
      mesh->addIndex(fourth);
    }
  }
  return mesh;
}

}
