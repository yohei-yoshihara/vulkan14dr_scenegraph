#ifndef __CAMERA_HPP__
#define __CAMERA_HPP__

#include "common.hpp"
#include <SDL3/SDL.h>

namespace b3 {

class Camera {
  glm::vec3 m_position = {0.0f, 0.0f, 0.0f};
  float m_yaw = 0.0f;   // degrees
  float m_pitch = 0.0f; // degrees
  // 移動速度
  float m_speed = 1.0f;
  // マウス感度
  float m_sensitivity = 0.1f;

public:
  static Camera lookAt(const glm::vec3 &eye, const glm::vec3 &center);
  void handleMouseEvent(const SDL_Event &e);
  void updateCameraMovement(float dt);
  glm::mat4 getCameraView() const;

  const glm::vec3& position() const { return m_position; }
  float yaw() const { return m_yaw; }
  float pitch() const { return m_pitch; }
  float speed() const { return m_speed; }
  float sensitivity() const { return m_sensitivity; }
};

} // namespace b3

#endif