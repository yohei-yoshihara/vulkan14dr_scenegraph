#ifndef __ENGINE_HPP__
#define __ENGINE_HPP__

#include "b3/common.hpp"

#include <VkBootstrap.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "b3/camera.hpp"
#include "b3/types.hpp"

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace b3 {

class Node;
class Mesh;
class Texture;

struct AllocatedBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
};

struct AllocatedImage {
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
};

struct MeshData {
  AllocatedBuffer vertexBuffer;
  AllocatedBuffer indexBuffer;
};

struct TextureData {
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VkImageView imageView = VK_NULL_HANDLE;
};

class Engine {
  static constexpr uint32_t MAX_NODES = 32;
  static constexpr uint32_t MAX_TEXTURES = 4096;
  static constexpr int SHADOWMAP_SIZE = 2048;
  static constexpr float lightFOV = 45.0f;
  static constexpr float zNear = 1.0f;
  static constexpr float zFar = 96.0f;

  // Depth bias (and slope) are used to avoid shadowing artifacts
  // Constant depth bias factor (always applied)
  static constexpr float depthBiasConstant = 1.25f;
  // Slope depth bias factor, applied depending on polygon's slope
  static constexpr float depthBiasSlope = 1.75f;

  struct SceneUBO_VS {
    glm::mat4 view = {0.f};
    glm::mat4 proj = {0.f};
    glm::vec3 lightPos = {1.f, 1.f, 1.f};
  };
  struct SceneUBO_FS {
    glm::vec3 lightColor = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float ambient = 0.1f;
  };

  struct ModelUBO {
    glm::mat4 model;
    glm::mat4 shadowMatrix;
    glm::uint32_t texIndex;
  };

  struct ShadowUniformBufferObject {
    glm::mat4 depthMVP;
  };

  struct SwapchainDimensions {
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
  };

  struct PerFrame {
    VkFence queue_submit_fence = VK_NULL_HANDLE;
    VkCommandPool primary_command_pool = VK_NULL_HANDLE;
    VkCommandBuffer primary_command_buffer = VK_NULL_HANDLE;
    VkSemaphore swapchain_acquire_semaphore = VK_NULL_HANDLE;
    VkSemaphore swapchain_release_semaphore = VK_NULL_HANDLE;

    VkDescriptorSet sceneDescriptorSet = VK_NULL_HANDLE;
    VkBuffer sceneUniformBuffer = VK_NULL_HANDLE;
    VmaAllocation sceneUniformBufferAllocation = VK_NULL_HANDLE;

    VkDescriptorSet modelDescriptorSet = VK_NULL_HANDLE;
    VkBuffer modelUniformBuffer = VK_NULL_HANDLE;
    VmaAllocation modelUniformBufferAllocation = VK_NULL_HANDLE;

    glm::mat4 depthMVP;
    VkDescriptorSet shadowDescriptorSet = VK_NULL_HANDLE;
    VkBuffer shadowUniformBuffer = VK_NULL_HANDLE;
    VmaAllocation shadowUniformBufferAllocation = VK_NULL_HANDLE;
  };

  struct Context {
    // Base resources
    vkb::Instance instance;
    SDL_Window *window = nullptr;
    vkb::PhysicalDevice physicalDevice;
    vkb::Device device;
    VkQueue queue = VK_NULL_HANDLE;
    vkb::Swapchain swapchain;
    SwapchainDimensions swapchainDimensions;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    int32_t graphicsQueueIndex = -1;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkImage> swapchainImages;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::vector<VkSemaphore> recycledSemaphores;
    std::vector<PerFrame> perFrame;
    uint32_t currentIndex = 0;

    // command pool for transfer
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // VMA
    VmaAllocator vmaAllocator = VK_NULL_HANDLE;

    // メッシュデータ
    std::unordered_map<std::shared_ptr<Mesh>, MeshData> meshBufferMap;

    // テクスチャデータ
    std::unordered_map<std::shared_ptr<Texture>, TextureData> textureMap;
    VkSampler textureSampler;

    // Descriptor Pool
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    // Scene UBO
    VkDescriptorSetLayout sceneDescriptorSetLayout = VK_NULL_HANDLE;
    size_t sceneUBOBufferSizeForVS = 0;

    // UBO
    VkDescriptorSetLayout modelDescriptorSetLayout = VK_NULL_HANDLE;
    size_t modelUBOBufferSizePerNode = 0;

    // Texture Resource Descriptor
    VkDescriptorSetLayout textureDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet textureDescriptorSet = VK_NULL_HANDLE;

    // depth resources
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkImage depthImage = VK_NULL_HANDLE;
    VmaAllocation depthAllocation = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    // MSAA color image (swap chainイメージと同じ数だけ必要となる)
    std::vector<VkImage> colorImages;
    std::vector<VmaAllocation> colorAllocations;
    std::vector<VkImageView> colorImageViews;

    // shadow map resources
    const VkFormat shadowDepthFormat = VK_FORMAT_D32_SFLOAT;
    VkImage shadowImage = VK_NULL_HANDLE;
    VmaAllocation shadowAllocation = VK_NULL_HANDLE;
    VkImageView shadowImageView = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout shadowDescriptorSetLayout = VK_NULL_HANDLE;
    size_t shadowUBOBufferSizePerNode = 0;
    VkSampler shadowSampler = VK_NULL_HANDLE;
  };

  struct ShadowMapPushDescriptorData {
    VkDescriptorBufferInfo bufferInfo;
  };

public:
  Engine();

  ~Engine();

  bool prepare();

  void mainLoop();

  void update();

  bool resize(const uint32_t width, const uint32_t height);

  void initInstance();

  void initDevice();

  void initVertexBuffer();
  void initTexture();

  void initUBO();
  void initDescriptorPool();

  // シーン向けのディスクリプタセット
  void initSceneDescriptorSetLayout();
  void initSceneUB();
  void initShadowSampler();
  void allocateSceneDescriptorSet();
  void bindSceneDescriptorSet();

  // モデル向けのディスクリプタセット
  void initModelDescriptorSetLayout();
  void initModelUB();
  void allocateModelDescriptorSet();
  void bindModelDescriptorSet();

  // シャドウ向けのディスクリプタセット
  void initShadowDescriptorSetLayout();
  void initShadowUB();
  void allocateShadowDescriptorSet();
  void bindShadowDescriptorSet();

  // テクスチャ向けのデスクリプタセット
  void initTextureDescriptorSetLayout();
  void allocateTextureDescriptorSet();
  void bindTextureDescriptorSet();

  /**
   * UBOサイズから最小のDynamic UBOアラインメントを計算する
   * @param uboSize UBOのサイズ
   * @return Dynamic UBOのアラインメント
   */
  size_t minDynamicUBOAlignment(size_t uboSize);

  void updateUBO(PerFrame &per_frame);

  void initPerFrame(PerFrame &per_frame);

  void teardownPerFrame(PerFrame &per_frame);

  void initSwapchain();

  VkShaderModule loadShaderModule(const char *path);

  void initPipeline();
  void initShadowPipeline();

  // MSAA付きカラーイメージを作成する
  void initColor();
  void initDepth();
  void initShadow();

  VkResult acquireNextSwapchainImage(uint32_t *image);

  void render(uint32_t swapchainIndex);
  void renderShadow(uint32_t swapchainIndex, VkCommandBuffer cmd);

  VkResult presentImage(uint32_t index);

  // Utility Methos
  void transitionImageLayout(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkAccessFlags2 srcAccessMask,
                             VkAccessFlags2 dstAccessMask,
                             VkPipelineStageFlags2 srcStage,
                             VkPipelineStageFlags2 dstStage);

  void transitionImageLayout(VkImage image, VkFormat format,
                             VkImageLayout oldLayout, VkImageLayout newLayout);

  VkSurfaceFormatKHR
  selectSurfaceFormat(VkPhysicalDevice gpu, VkSurfaceKHR surface,
                      std::vector<VkFormat> const &preferred_formats = {
                          VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
                          VK_FORMAT_A8B8G8R8_SRGB_PACK32});

  VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates,
                               VkImageTiling tiling,
                               VkFormatFeatureFlags features);
  VkFormat findDepthFormat();

  // ワンショットコマンドバッファの開始
  VkCommandBuffer beginSingleTimeCommands();

  // ワンショットコマンドバッファの終了
  void endSingleTimeCommands(VkCommandBuffer commandBuffer);

  // バッファの作成
  AllocatedBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                               VmaMemoryUsage memoryUsage);

  // バッファーのコピー
  void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

  // バッファの作成と初期データの設定
  AllocatedBuffer uploadBuffer(const void *data, VkDeviceSize size,
                               VkBufferUsageFlags usage = 0);

  // イメージの作成
  AllocatedImage
  createImage(uint32_t width, uint32_t height, uint32_t mipLevels,
              VkSampleCountFlagBits numSamples, VkFormat format,
              VkImageTiling tiling, VkImageUsageFlags usage,
              VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO);
  // バッファのイメージへのコピー
  void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                         uint32_t height);

  // MSAAの最大サンプル数の取得
  VkSampleCountFlagBits getMaxUsableSampleCount();

  // 異方性フィルタリングの最大数の取得
  float getMaxSamplerAnisotropy();

  // ***** シーングラフ *****

  // add a node to scene graph
  void addNode(const std::shared_ptr<Node> &node);

  void setWindowSize(uint32_t width, uint32_t height) {
    m_windowWidth = width;
    m_windowHeight = height;
  }

  void setCameraPosition(const glm::vec3 &eye, const glm::vec3 &center) {
    m_camera = Camera::lookAt(eye, center);
  }

  void setLightPos(const glm::vec4 &lightPos) { m_lightPos = lightPos; }

private:
  Context m_context;
  VkSampleCountFlagBits m_msaaSamples = VK_SAMPLE_COUNT_1_BIT;

  // nodes
  std::vector<std::shared_ptr<Node>> m_nodes;

  // 影を落とすノードかどうかのフラグ
  std::vector<bool> m_shadowCastingNodes;
  // カメラに写っているノードかどうかのフラグ
  std::vector<bool> m_visibleNodes;

  // window size
  uint32_t m_windowWidth = 1024;
  uint32_t m_windowHeight = 768;

  // light position
  glm::vec3 m_lightPos = {0.0f, 5.0f, 5.0f};
  glm::vec3 m_lightColor = {1.0f, 1.0f, 1.0f};
  float m_intensity = 1.0f;
  float m_ambient = 0.1f;

  Camera m_camera = Camera::lookAt({1.7f, 1.7f, 1.0f}, {0.0f, 0.0f, 0.0});
};

} // namespace b3

#endif