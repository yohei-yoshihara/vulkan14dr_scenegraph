#include "texture.hpp"

#include <stb_image.h>

namespace b3 {

Texture::Texture(const std::string &filename, bool sRGB) : m_sRGB(sRGB) {
  int width, height, nComponents;
  auto *data =
      stbi_load(filename.c_str(), &width, &height, &nComponents, STBI_rgb_alpha);
  if (data == nullptr) {
    SPDLOG_ERROR("Failed to load {}", filename);
    throw std::runtime_error("texture creation error");
  }

  m_width = width;
  m_height = height;

  if (nComponents == 3) {
    // PNGファイルにアルファがない場合はn=3になる
    m_pixels.resize(width * height * 4);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        size_t src = (x + y * width) * 3;
        size_t dst = (x + y * width) * 4;
        assert(dst < m_pixels.size());
        m_pixels[dst + 0] = data[src + 0];
        m_pixels[dst + 1] = data[src + 1];
        m_pixels[dst + 2] = data[src + 2];
        m_pixels[dst + 3] = 255;
      }
    }
    stbi_image_free(data);
    return;
  } else if (nComponents == 4) {
    const auto len = width * height * 4;
    m_pixels.resize(len);
    std::memcpy(m_pixels.data(), data, len);
    stbi_image_free(data);
    return;
  }

  SPDLOG_ERROR("Only support rgb or rgba format");
  throw std::runtime_error("texture creation error");
}

constexpr size_t COLOR_TEXTURE_WIDTH = 4;
constexpr size_t COLOR_TEXTURE_HEIGHT = 4;

Texture::Texture(const RGBAColor &color)
    : m_width(COLOR_TEXTURE_WIDTH), m_height(COLOR_TEXTURE_HEIGHT),
      m_sRGB(false), m_pixels(COLOR_TEXTURE_WIDTH * COLOR_TEXTURE_HEIGHT * 4) {
  for (size_t y = 0; y < COLOR_TEXTURE_HEIGHT; ++y) {
    for (size_t x = 0; x < COLOR_TEXTURE_WIDTH; ++x) {
      auto dst = (x + y * COLOR_TEXTURE_WIDTH) * 4;
      assert(dst < m_pixels.size());
      m_pixels[dst + 0] = color.r * 255;
      m_pixels[dst + 1] = color.g * 255;
      m_pixels[dst + 2] = color.b * 255;
      m_pixels[dst + 3] = color.a * 255;
    }
  }
}

} // namespace b3
