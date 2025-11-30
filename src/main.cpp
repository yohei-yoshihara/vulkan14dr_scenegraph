#include "main.hpp"

#include "b3/b3.hpp"

#include <iostream>
#include <memory>
#include <filesystem>

using namespace b3;

int main(int argc, char *argv[]) {
  Engine engine;
  {
    auto mesh = mesh::PlaneMesh::generate(6, 6, UpAxis::Z, 1, 1);
    //auto texture = std::make_shared<Texture>(RGBAColor{.r = 1.f, .g = 0.f, .b = 0.f, .a = 1.f});
    auto texture = std::make_shared<Texture>("images/floor.png", true);
    auto node = std::make_shared<Node>(mesh, texture);
    node->setPosition(glm::vec3(0, 0, -0.5));
    node->setEulerAngle(glm::vec3(0, 0, 0));
    engine.addNode(node);
  }
  {
    auto mesh = mesh::SphereMesh::generate(0.5, 32, 32);
    auto texture = std::make_shared<Texture>(
        RGBAColor{.r = 0.f, .g = 1.f, .b = 0.f, .a = 1.f});
    auto node = std::make_shared<Node>(mesh, texture);
    node->setPosition(glm::vec3(1, 0, 0));
    node->setEulerAngle(glm::vec3(0, 0, 0));
    engine.addNode(node);
  }
  {
    auto mesh = mesh::CubeMesh::generate(1.0f, 1.0f, 1.0f, 32, 32);
    auto texture = std::make_shared<Texture>(
        RGBAColor{.r = 0.f, .g = 0.f, .b = 1.f, .a = 1.f});
    auto node = std::make_shared<Node>(mesh, texture);
    node->setPosition(glm::vec3(-1, 0, 0));
    node->setEulerAngle(glm::vec3(0, 0, 0));
    engine.addNode(node);
  }
  engine.prepare();
  engine.mainLoop();
  return 0;
}
