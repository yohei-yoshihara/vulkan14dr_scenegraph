#include "mesh.hpp"

namespace b3 {

IndexType Mesh::addVertex(const Vertex &vertex) {
  m_vertices.push_back(vertex);
  return static_cast<IndexType>(m_vertices.size() - 1);
}

void Mesh::addIndex(IndexType index) {
  m_indices.push_back(static_cast<IndexType>(index));
}

}