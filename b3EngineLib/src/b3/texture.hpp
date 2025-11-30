#ifndef __TEXTURE_HPP__
#define __TEXTURE_HPP__

#include <memory>

#include "b3/types.hpp"
#include "b3/common.hpp"

namespace b3 {

class Texture {
  uint32_t m_width;
  uint32_t m_height;
  bool m_sRGB;
  std::vector<uint8_t> m_pixels;
  VkImage m_image = VK_NULL_HANDLE;
  VkImageView m_imageView = VK_NULL_HANDLE;
  VmaAllocation m_allocation = VK_NULL_HANDLE;

public:
  Texture(const std::string &filename, bool sRGB);
  Texture(const RGBAColor &color);

  uint32_t width() const { return m_width; }
  uint32_t height() const { return m_height; }
  bool sRGB() const { return m_sRGB; }
  const uint8_t *pixels() { return m_pixels.data(); }

  VkImage getImage() const { return m_image; }
  VkImageView getImageView() const { return m_imageView; }
  VmaAllocation getAllocation() const { return m_allocation; }
  void setImage(VkImage image) { m_image = image; }
  void setImageView(VkImageView imageView) { m_imageView = imageView; }
  void setAllocation(VmaAllocation allocation) { m_allocation = allocation; }
};

} // namespace b3

#endif