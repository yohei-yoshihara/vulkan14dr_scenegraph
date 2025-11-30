#include "CubeMesh.hpp"

namespace b3::mesh {

IndexType buildPlane(IndexType startingIndice, float width, float height,
                     float translateX, float translateY, float translateZ,
                     float normalX, float normalY, float normalZ,
                     bool clockwise, size_t nx, size_t ny, Mesh &mesh) {
  for (size_t i = 0; i <= nx; ++i) {
    float x = -(width * 0.5f) + i * (width / nx);
    float u = 1.0f - (1.0f * i / nx);

    for (size_t j = 0; j <= ny; ++j) {
      float y = -(height * 0.5f) + j * (height / ny);
      float v = 1.0f * j / ny;

      Vertex vertex;
      if (normalZ != 0.0f) {
        vertex.position = {
          translateX + x,
          translateY + y,
          translateZ
        };
      } else if (normalY != 0.0f) {
        vertex.position = { 
          translateX + y,
          translateY,
          translateZ + x,
        };
      } else {
        vertex.position = {
          translateX,
          translateY + x,
          translateZ + y
        };
      }
      vertex.normal = {
        normalX,
        normalY,
        normalZ
      };
      vertex.texCoord = {u, v};
      mesh.addVertex(vertex);
    }
  }

  for (size_t i = 0; i < nx; ++i) {
    for (size_t j = 0; j < ny; ++j) {

      int first = startingIndice + j * (nx + 1) + i;
      int second = first + (nx + 1);
      int third = first + 1;
      int fourth = second + 1;

      if (clockwise == true) {
        mesh.addIndex(first);
        mesh.addIndex(third);
        mesh.addIndex(second);

        mesh.addIndex(third);
        mesh.addIndex(fourth);
        mesh.addIndex(second);
      } else {
        mesh.addIndex(first);
        mesh.addIndex(second);
        mesh.addIndex(third);

        mesh.addIndex(third);
        mesh.addIndex(second);
        mesh.addIndex(fourth);
      }
    }
  }

  return startingIndice + (nx + 1) * (ny + 1);
}

std::shared_ptr<Mesh> CubeMesh::generate(float width, float height, float depth, size_t nx, size_t ny) {
  auto mesh = std::make_shared<Mesh>();

  IndexType indice = 0;
  // Top
  indice = buildPlane(0, width, height, 0.0f, 0.0f, depth * 0.5f, 0.0f, 0.0f, 1.0f, false, nx, ny, *mesh);
  // Bottom
  indice = buildPlane(indice, width, height, 0.0f, 0.0f, -depth * 0.5f, 0.0f, 0.0f,
                      -1.0f, true, nx, ny, *mesh);

  // Right
  indice = buildPlane(indice, height, depth, width * 0.5f, 0.0f, 0.0f,
                      1.0f, 0.0f, 0.0f, false, nx, ny, *mesh);
  // Left
  indice = buildPlane(indice, 
    height, depth, 
    -width * 0.5f, 0.0f, 0.0f, 
    -1.0f, 0.0f, 0.0f, 
    true, 
    nx, ny, *mesh);

  // Front
  indice = buildPlane(indice, depth, width, 0.0f, height * 0.5f, 0.0f, 0.0f,
                      1.0f, 0.0f, false, nx, ny, *mesh);
  // Rear
  indice = buildPlane(indice, depth, width, 0.0f, -height * 0.5f, 0.0f, 0.0f,
                      -1.0f, 0.0f, true, nx, ny, *mesh);
  return mesh;
}

}
