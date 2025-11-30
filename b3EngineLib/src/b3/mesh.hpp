#ifndef __MESH2_HPP__
#define __MESH2_HPP__

#include "common.hpp"
#include "types.hpp"

namespace b3 {

class Mesh {
  std::vector<Vertex> m_vertices;
  std::vector<IndexType> m_indices;

public:
  IndexType addVertex(const Vertex &vertex);
  void addIndex(IndexType index);

  const std::vector<Vertex> vertices() const { return m_vertices; }
  const std::vector<IndexType> indices() const { return m_indices; }
  const Vertex &vertex(size_t i) const { return m_vertices[i]; }
  IndexType index(size_t i) const { return m_indices[i]; }
  size_t numberOfVertices() const { return m_vertices.size(); }
  size_t numberOfIndices() const { return m_indices.size(); }
};

} // namespace b3

#endif