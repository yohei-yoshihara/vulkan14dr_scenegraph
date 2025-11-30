#include "PlaneMesh.hpp"

namespace b3::mesh {

std::shared_ptr<Mesh> PlaneMesh::generate(float width, float height, UpAxis up,
                                          int nx, int ny,
                                          UVMap uvMap) {
  auto mesh = std::make_shared<Mesh>();

  auto ab = uvMap.b - uvMap.a;
  auto ac = uvMap.c - uvMap.a;

  for (int i = 0; i <= nx; ++i) {
    float x = -(width / 2) + i * (width / nx);
    float iRatio = static_cast<float>(i) / nx;
    auto uvX = uvMap.a + iRatio * ab;

    for (int j = 0; j <= ny; ++j) {
      float y = -(height / 2) + j * (height / ny);
      auto jRatio = 1.f - static_cast<float>(j) / ny;
      auto uXY = uvX + jRatio * ac;
      mesh->addVertex({{x, y, 0.0f}, {0.0f, 0.0f, 1.0f}, uXY});
    }
  }

  for (int i = 0; i < nx; ++i) {
    for (int j = 0; j < ny; ++j) {

      int first = i * (ny + 1) + j;
      int second = first + (ny + 1);
      int third = first + 1;
      int fourth = second + 1;

      mesh->addIndex(first);
      mesh->addIndex(second);
      mesh->addIndex(third);

      mesh->addIndex(third);
      mesh->addIndex(second);
      mesh->addIndex(fourth);
    }
  }

  return mesh;
}

} // namespace b3::mesh
