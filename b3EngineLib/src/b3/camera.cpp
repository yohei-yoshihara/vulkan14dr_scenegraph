#include "camera.hpp"

namespace b3 {

Camera Camera::lookAt(const glm::vec3 &eye, const glm::vec3 &center) {
  Camera cam;
  cam.m_position = eye;

  // --- 方向ベクトル ---
  glm::vec3 dir = glm::normalize(center - eye);

  // --- yaw（水平角）---
  // atan2(y, x) なので Z-up / Y-up に関係なく正しい水平角が出る
  cam.m_yaw = glm::degrees(std::atan2(dir.y, dir.x));

  // --- pitch（上下角）---
  // 水平方向の長さ
  float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);

  cam.m_pitch = glm::degrees(std::atan2(dir.z, len));

  return cam;
}

void Camera::handleMouseEvent(const SDL_Event &e) {
  if (e.type == SDL_EVENT_MOUSE_MOTION) {
    float dx = e.motion.xrel * m_sensitivity;
    float dy = e.motion.yrel * m_sensitivity;

    m_yaw += -dx;   // 左右
    m_pitch -= dy; // 上下（マウス上で pitch +）

    // ピッチ制限（上90° 下-90°）
    if (m_pitch > 89.0f) {
      m_pitch = 89.0f;
    }
    if (m_pitch < -89.0f) {
      m_pitch = -89.0f;
    }
  }
}

void Camera::updateCameraMovement(float dt) {
  const bool *k = SDL_GetKeyboardState(nullptr);

  // --- 前方ベクトル（Z-Up版） ---
  glm::vec3 front;
  front.x = std::cos(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
  front.y = std::sin(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
  front.z = std::sin(glm::radians(m_pitch));
  front = glm::normalize(front);

  // --- 右方向（Z-Up版）---
  glm::vec3 up(0.0f, 0.0f, 1.0f);
  glm::vec3 right = glm::normalize(glm::cross(front, up));

  // --- WASD 移動 ---
  if (k[SDL_SCANCODE_W]) {
    m_position += front * m_speed * dt;
  }
  if (k[SDL_SCANCODE_S]) {
    m_position -= front * m_speed * dt;
  }
  if (k[SDL_SCANCODE_A]) {
    m_position -= right * m_speed * dt;
  }
  if (k[SDL_SCANCODE_D]) {
    m_position += right * m_speed * dt;
  }
}

glm::mat4 Camera::getCameraView() const {
  glm::vec3 front;
  front.x = std::cos(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
  front.y = std::sin(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
  front.z = std::sin(glm::radians(m_pitch));

  glm::vec3 center = m_position + glm::normalize(front);

  return glm::lookAt(m_position, center, glm::vec3(0.f, 0.f, 1.f));
}

}