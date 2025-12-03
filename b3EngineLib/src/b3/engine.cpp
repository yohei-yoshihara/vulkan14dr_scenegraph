#include "engine.hpp"

#include "b3/common.hpp"
#include "b3/frustum_culling.hpp"
#include "b3/mesh.hpp"
#include "b3/node.hpp"
#include "b3/texture.hpp"

#include <SDL3/SDL_mouse.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <ranges>

namespace b3 {

auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
auto file_sink =
    std::make_shared<spdlog::sinks::basic_file_sink_mt>("app.log", true);
std::shared_ptr<spdlog::logger> logger;

Engine::Engine() {
  std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};

  logger =
      std::make_shared<spdlog::logger>("logger", sinks.begin(), sinks.end());
  spdlog::set_default_logger(logger);
#ifdef _DEBUG
  spdlog::set_level(spdlog::level::debug);
#else
  spdlog::set_level(spdlog::level::info);
#endif
}

void Engine::initInstance() {
  LOGI("Initializing Vulkan instance.");

  std::vector<const char *> required_instance_extensions{
      VK_KHR_SURFACE_EXTENSION_NAME,
      // VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
  };

  uint32_t sdlExtensionCount = 0;
  auto sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
  std::copy(sdlExtensions, sdlExtensions + sdlExtensionCount,
            std::back_inserter(required_instance_extensions));

  vkb::InstanceBuilder builder;
  auto inst_ret =
      builder.set_app_name("Simple Scene Graph V1.3 + Direct Rendering")
          .set_engine_name("No Engine")
          .enable_extensions(required_instance_extensions)
          .require_api_version(VK_MAKE_VERSION(1, 3, 0))
          .build();
  if (!inst_ret) {
    throw std::runtime_error("failed to create instance");
  }
  m_context.instance = inst_ret.value();

  volkLoadInstance(m_context.instance);
}

void Engine::initDevice() {
  LOGI("Initializing Vulkan device.");

  VkPhysicalDeviceVulkan13Features features13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .synchronization2 = VK_TRUE,
      .dynamicRendering = VK_TRUE,
  };

  VkPhysicalDeviceVulkan12Features features12{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .descriptorIndexing = VK_TRUE,
      .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
      .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
      .descriptorBindingUpdateUnusedWhilePending = VK_TRUE,
      .descriptorBindingPartiallyBound = VK_TRUE,
      .descriptorBindingVariableDescriptorCount = VK_TRUE,
      .runtimeDescriptorArray = VK_TRUE,
  };

  VkPhysicalDeviceFeatures features10{
      .samplerAnisotropy = VK_TRUE,
  };

  vkb::PhysicalDeviceSelector selector{m_context.instance};
  auto phys_ret = selector.set_minimum_version(1, 3)
                      .set_required_features_13(features13)
                      .set_required_features_12(features12)
                      .set_required_features(features10)
                      .set_surface(m_context.surface)
                      .select();
  if (!phys_ret) {
    LOGE("failed to select Vulkan Physical Device");
    assert(false);
    return;
  }
  m_context.physicalDevice = phys_ret.value();
  m_msaaSamples = getMaxUsableSampleCount();

  vkb::DeviceBuilder device_builder{phys_ret.value()};
  auto dev_ret = device_builder.build();
  if (!dev_ret) {
    LOGE("Failed to create Vulkan device");
    throw std::runtime_error("Failed to create Vulkan device");
  }
  m_context.device = dev_ret.value();

  auto graphics_queue_ret =
      m_context.device.get_queue(vkb::QueueType::graphics);
  if (!graphics_queue_ret) {
    LOGE("Failed to get graphics queue");
    return;
  }
  m_context.graphicsQueueIndex =
      m_context.device.get_queue_index(vkb::QueueType::graphics).value();
  m_context.queue = graphics_queue_ret.value();

  VkCommandPoolCreateInfo cmd_pool_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = static_cast<uint32_t>(m_context.graphicsQueueIndex)};
  VK_CHECK(vkCreateCommandPool(m_context.device, &cmd_pool_info, nullptr,
                               &m_context.commandPool));

  VmaVulkanFunctions functions{
      .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
      .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
  };

  VmaAllocatorCreateInfo createInfo{
      .physicalDevice = m_context.physicalDevice,
      .device = m_context.device,
      .pVulkanFunctions = &functions,
      .instance = m_context.instance,
      .vulkanApiVersion = VK_API_VERSION_1_3,
  };
  VK_CHECK(vmaCreateAllocator(&createInfo, &m_context.vmaAllocator));
}

/**
 * Vertex Bufferの初期化
 */
void Engine::initVertexBuffer() {
  for (const auto &node : m_nodes) {
    const auto &mesh = node->mesh();
    if (!m_context.meshBufferMap.contains(mesh)) {
      auto vertex = uploadBuffer(mesh->vertices().data(),
                                 mesh->vertices().size() * sizeof(Vertex),
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
      auto index = uploadBuffer(mesh->indices().data(),
                                mesh->indices().size() * sizeof(IndexType),
                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
      MeshData meshBuffer{vertex, index};
      m_context.meshBufferMap[mesh] = meshBuffer;
    }
  }
}

void Engine::initTexture() {
  for (const auto &node : m_nodes) {
    const auto &texture = node->texture();
    VkDeviceSize size = texture->width() * texture->height() * 4;
    // ステージングバッファの作成
    auto staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU);
    // 画像データのステージングバッファへのコピー
    VK_CHECK(vmaCopyMemoryToAllocation(m_context.vmaAllocator,
                                       texture->pixels(), staging.allocation, 0,
                                       size));
    VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = texture->sRGB() ? VK_FORMAT_R8G8B8A8_SRGB
                                  : VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {texture->width(), texture->height(), 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo allocationCreateInfo = {
        .flags = 0,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = 0,
    };
    VkImage textureImage;
    VmaAllocation allocation;
    // イメージの作成
    VK_CHECK(vmaCreateImage(m_context.vmaAllocator, &imageInfo,
                            &allocationCreateInfo, &textureImage, &allocation,
                            nullptr));

    // イメージを転送先に最適化する
    transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    // バッファからイメージへコピーする
    copyBufferToImage(staging.buffer, textureImage, texture->width(),
                      texture->height());
    // イメージをシェーダー読み込みに最適化する
    transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // ステージングバッファの削除
    vmaDestroyBuffer(m_context.vmaAllocator, staging.buffer,
                     staging.allocation);

    // VkImageViewの作成
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    VkImageView imageView;
    VK_CHECK(
        vkCreateImageView(m_context.device, &viewInfo, nullptr, &imageView));
    assert(imageView != VK_NULL_HANDLE);

    TextureData textureData = {.image = textureImage,
                               .allocation = allocation,
                               .imageView = imageView};
    m_context.textureMap[texture] = textureData;
  }

  // VkSamplerの作成
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = getMaxSamplerAnisotropy();
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  VK_CHECK(vkCreateSampler(m_context.device, &samplerInfo, nullptr,
                           &m_context.textureSampler));
}

/**
 * Uniform Buffer Objectの初期化
 */
void Engine::initUBO() {
  initDescriptorPool();

  initSceneDescriptorSetLayout();
  initShadowDescriptorSetLayout();

  initSceneUB();
  initShadowUB();

  initShadowSampler();

  allocateSceneDescriptorSet();
  bindSceneDescriptorSet();

  allocateShadowDescriptorSet();
  bindShadowDescriptorSet();

  initModelDescriptorSetLayout();
  initModelUB();
  allocateModelDescriptorSet();
  bindModelDescriptorSet();

  initTextureDescriptorSetLayout();
  allocateTextureDescriptorSet();
  bindTextureDescriptorSet();
}

void Engine::initDescriptorPool() {
  // * DescriptorPoolの作成
  auto image_count = m_context.swapchain.image_count;

  std::vector<VkDescriptorPoolSize> poolSizes = {
      {
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 2 * image_count,
      },
      {
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
          .descriptorCount = (1 + 1) * image_count,
      },
      {
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = image_count
      },
      {
          .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .descriptorCount = MAX_TEXTURES * image_count},
      {
          .type = VK_DESCRIPTOR_TYPE_SAMPLER,
          .descriptorCount = image_count,
      },
  };
  // 最大セット数
  auto maxSets = (2 + 1) * image_count + 1;
  VkDescriptorPoolCreateInfo poolInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      .maxSets = maxSets,
      .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
      .pPoolSizes = poolSizes.data(),
  };

  VK_CHECK(vkCreateDescriptorPool(m_context.device, &poolInfo, nullptr,
                                  &m_context.descriptorPool));
}

// ***** シーン向けのディスクリプタセット *****

void Engine::initSceneDescriptorSetLayout() {
  // シーンシェーダーが必要とするUniformを定義する。
  std::vector<VkDescriptorSetLayoutBinding> uboLayoutBindings = {
      {
          // UBO(MVPマトリックスなど)
          .binding = 0, // バインディング位置 (`layout(binding = 0) uniform
                        // ...`の0と一致)
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1, // 配列の要素数（配列でなければ1）
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .pImmutableSamplers = nullptr,
      },
      {
          // シャドウマップテクスチャ
          .binding = 1, // バインディング位置 (`layout(binding = 0) uniform
                        // ...`の0と一致)
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1, // 配列の要素数（配列でなければ1）
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .pImmutableSamplers = nullptr,
      },
      {
          // UBO(MVPマトリックスなど)
          .binding = 2, // バインディング位置 (`layout(binding = 0) uniform
                        // ...`の0と一致)
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1, // 配列の要素数（配列でなければ1）
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .pImmutableSamplers = nullptr,
      },
  };

  VkDescriptorSetLayoutCreateInfo layoutInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .bindingCount = static_cast<uint32_t>(uboLayoutBindings.size()),
      .pBindings = uboLayoutBindings.data(),
  };
  VK_CHECK(vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, nullptr,
                                       &m_context.sceneDescriptorSetLayout));
}

// シーン向けのUniform Bufferの初期化
void Engine::initSceneUB() {
  const auto image_count = m_context.swapchain.image_count;
  m_context.sceneUBOBufferSizeForVS =
      minDynamicUBOAlignment(sizeof(SceneUBO_VS));

  for (size_t i = 0; i < image_count; ++i) {
    auto &per_frame = m_context.perFrame[i];

    VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = m_context.sceneUBOBufferSizeForVS + sizeof(SceneUBO_FS),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo allocationCreateInfo = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };

    VK_CHECK(vmaCreateBuffer(m_context.vmaAllocator, &bufferCreateInfo,
                             &allocationCreateInfo,
                             &per_frame.sceneUniformBuffer,
                             &per_frame.sceneUniformBufferAllocation, nullptr));
  }
}

void Engine::initShadowSampler() {
  // サンプラーの生成
  VkSamplerCreateInfo samplerInfo = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,

      // フィルタリングモード
      .magFilter = VK_FILTER_LINEAR, // 拡大時: 線形補間
      .minFilter = VK_FILTER_LINEAR, // 縮小時: 線形補間

      // Mipmap設定
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR, // Mipmap間の補間

      // アドレッシングモード (UV範囲外アクセス時)
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,

      .mipLodBias = 0.0f,
      .maxAnisotropy = 1.0f,
      .minLod = 0.0f,
      .maxLod = 1.0f,

      // 境界色（CLAMP_TO_BORDER時のみ有効）
      .borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
  };

  VK_CHECK(vkCreateSampler(m_context.device, &samplerInfo, nullptr,
                           &m_context.shadowSampler));
}

void Engine::allocateSceneDescriptorSet() {
  // シーンのためのDescriptor Setを作成する
  const auto image_count = m_context.swapchain.image_count;
  std::vector<VkDescriptorSetLayout> layouts(
      image_count, m_context.sceneDescriptorSetLayout);

  VkDescriptorSetAllocateInfo allocInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = m_context.descriptorPool,
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data(),
  };
  std::vector<VkDescriptorSet> descriptorSets(m_context.swapchain.image_count,
                                              VK_NULL_HANDLE);
  VK_CHECK(vkAllocateDescriptorSets(m_context.device, &allocInfo,
                                    descriptorSets.data()));
  for (size_t i = 0; i < image_count; ++i) {
    m_context.perFrame[i].sceneDescriptorSet = descriptorSets[i];
  }
}

void Engine::bindSceneDescriptorSet() {
  const auto image_count = m_context.swapchain.image_count;
  for (size_t i = 0; i < image_count; ++i) {
    auto &per_frame = m_context.perFrame[i];

    std::vector<VkDescriptorBufferInfo> vsBufferInfos = {
        {
            .buffer = per_frame.sceneUniformBuffer,
            .offset = 0,
            .range = sizeof(SceneUBO_VS),
        },
    };

    std::vector<VkDescriptorImageInfo> shadowMapImageInfos = {{
        .sampler = m_context.shadowSampler,     // VkSampler
        .imageView = m_context.shadowImageView, // VkImageView
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    }};

    std::vector<VkDescriptorBufferInfo> fsBufferInfos = {
        {
            .buffer = per_frame.sceneUniformBuffer,
            .offset = m_context.sceneUBOBufferSizeForVS,
            .range = sizeof(SceneUBO_FS),
        },
    };

    std::vector<VkWriteDescriptorSet> descriptorWrites = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = per_frame.sceneDescriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = static_cast<uint32_t>(vsBufferInfos.size()),
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = vsBufferInfos.data(),
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = per_frame.sceneDescriptorSet,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount =
                static_cast<uint32_t>(shadowMapImageInfos.size()),
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = shadowMapImageInfos.data(),
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = per_frame.sceneDescriptorSet,
            .dstBinding = 2,
            .dstArrayElement = 0,
            .descriptorCount = static_cast<uint32_t>(fsBufferInfos.size()),
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = fsBufferInfos.data(),
        },
    };

    // シーンのディスクリプタセットの更新
    vkUpdateDescriptorSets(m_context.device,
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);
  } // image_count
}

// ***** モデル向けのディスクリプタセット *****

void Engine::initModelDescriptorSetLayout() {
  // シーンシェーダーが必要とするUniformを定義する。
  std::vector<VkDescriptorSetLayoutBinding> uboLayoutBindings = {
      {
          // UBO(MVPマトリックスなど)
          .binding = 0, // バインディング位置 (`layout(binding = 0) uniform
                        // ...`の0と一致)
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
          .descriptorCount = 1, // 配列の要素数（配列でなければ1）
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .pImmutableSamplers = nullptr,
      },
  };

  VkDescriptorSetLayoutCreateInfo layoutInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = static_cast<uint32_t>(uboLayoutBindings.size()),
      .pBindings = uboLayoutBindings.data(),
  };
  VK_CHECK(vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, nullptr,
                                       &m_context.modelDescriptorSetLayout));
}

void Engine::initModelUB() {
  m_context.modelUBOBufferSizePerNode =
      minDynamicUBOAlignment(sizeof(ModelUBO));
  VkDeviceSize totalBufferSize =
      MAX_NODES * m_context.modelUBOBufferSizePerNode;

  const auto image_count = m_context.swapchain.image_count;
  for (size_t i = 0; i < image_count; ++i) {
    auto &per_frame = m_context.perFrame[i];

    VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = totalBufferSize,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo allocationCreateInfo = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };

    VK_CHECK(vmaCreateBuffer(m_context.vmaAllocator, &bufferCreateInfo,
                             &allocationCreateInfo,
                             &per_frame.modelUniformBuffer,
                             &per_frame.modelUniformBufferAllocation, nullptr));
  }
}

void Engine::allocateModelDescriptorSet() {
  // シーンのためのDescriptor Setを作成する
  const auto image_count = m_context.swapchain.image_count;
  std::vector<VkDescriptorSetLayout> layouts(
      image_count, m_context.modelDescriptorSetLayout);

  VkDescriptorSetAllocateInfo allocInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = m_context.descriptorPool,
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data(),
  };
  std::vector<VkDescriptorSet> descriptorSets(m_context.swapchain.image_count,
                                              VK_NULL_HANDLE);
  VK_CHECK(vkAllocateDescriptorSets(m_context.device, &allocInfo,
                                    descriptorSets.data()));
  for (size_t i = 0; i < image_count; ++i) {
    m_context.perFrame[i].modelDescriptorSet = descriptorSets[i];
  }
}

void Engine::bindModelDescriptorSet() {
  const auto image_count = m_context.swapchain.image_count;
  for (size_t i = 0; i < image_count; ++i) {
    auto &per_frame = m_context.perFrame[i];

    std::vector<VkDescriptorBufferInfo> bufferInfos = {
        {
            .buffer = per_frame.modelUniformBuffer,
            .offset = 0,
            .range = sizeof(ModelUBO),
        },
    };
    std::vector<VkWriteDescriptorSet> descriptorWrites = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = per_frame.modelDescriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = static_cast<uint32_t>(bufferInfos.size()),
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo = bufferInfos.data(),
        },
    };

    // シーンのディスクリプタセットの更新
    vkUpdateDescriptorSets(m_context.device,
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);
  } // image_count
}

// ***** シャドウ向けのディスクリプタセット *****

void Engine::initShadowDescriptorSetLayout() {
  // シャドウシェーダーが必要とするUniformを定義する。
  std::vector<VkDescriptorSetLayoutBinding> shadowUBOLayoutBindings = {{
      .binding =
          0, // バインディング位置 (`layout(binding = 0) uniform ...`の0と一致)
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
      .descriptorCount = 1, // 配列の要素数（配列でなければ1）
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      .pImmutableSamplers = nullptr,
  }};
  VkDescriptorSetLayoutCreateInfo shadowLayoutInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = static_cast<uint32_t>(shadowUBOLayoutBindings.size()),
      .pBindings = shadowUBOLayoutBindings.data(),
  };
  VK_CHECK(vkCreateDescriptorSetLayout(m_context.device, &shadowLayoutInfo,
                                       nullptr,
                                       &m_context.shadowDescriptorSetLayout));
}

void Engine::initShadowUB() {
  m_context.shadowUBOBufferSizePerNode =
      minDynamicUBOAlignment(sizeof(ShadowUniformBufferObject));
  VkDeviceSize shadowTotalBufferSize =
      MAX_NODES * m_context.shadowUBOBufferSizePerNode;

  // シャドウ向けのUniform Bufferの作成
  const auto image_count = m_context.swapchain.image_count;
  for (size_t i = 0; i < image_count; ++i) {
    auto &per_frame = m_context.perFrame[i];

    VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = shadowTotalBufferSize,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo allocationCreateInfo = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };

    VK_CHECK(
        vmaCreateBuffer(m_context.vmaAllocator, &bufferCreateInfo,
                        &allocationCreateInfo, &per_frame.shadowUniformBuffer,
                        &per_frame.shadowUniformBufferAllocation, nullptr));
  }
}

void Engine::allocateShadowDescriptorSet() {
  // シャドウのためのDescriptor Setを作成する
  const auto image_count = m_context.swapchain.image_count;
  std::vector<VkDescriptorSetLayout> shadowLayouts(
      image_count, m_context.shadowDescriptorSetLayout);
  VkDescriptorSetAllocateInfo shadowAllocInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = m_context.descriptorPool,
      .descriptorSetCount = static_cast<uint32_t>(shadowLayouts.size()),
      .pSetLayouts = shadowLayouts.data(),
  };
  std::vector<VkDescriptorSet> shadowDescriptorSets(image_count,
                                                    VK_NULL_HANDLE);
  VK_CHECK(vkAllocateDescriptorSets(m_context.device, &shadowAllocInfo,
                                    shadowDescriptorSets.data()));
  for (size_t i = 0; i < image_count; ++i) {
    auto &per_frame = m_context.perFrame[i];
    per_frame.shadowDescriptorSet = shadowDescriptorSets[i];
  }
}

void Engine::bindShadowDescriptorSet() {
  // シャドウ
  const auto image_count = m_context.swapchain.image_count;
  for (size_t i = 0; i < image_count; ++i) {
    auto &per_frame = m_context.perFrame[i];

    std::vector<VkDescriptorBufferInfo> bufferInfos = {
        {
            .buffer = per_frame.shadowUniformBuffer,
            .offset = 0,
            .range = sizeof(ShadowUniformBufferObject),
        },
    };

    std::vector<VkWriteDescriptorSet> descriptorWrites = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = per_frame.shadowDescriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = static_cast<uint32_t>(bufferInfos.size()),
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo = bufferInfos.data(),
        },
    };

    // シャドウのディスクリプタセットの更新
    vkUpdateDescriptorSets(m_context.device,
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);
  } // image_count
}

// ***** テクスチャのためのディスクリプタセット *****

void Engine::initTextureDescriptorSetLayout() {
  std::vector<VkDescriptorSetLayoutBinding> uboLayoutBindings = {
      {
          // マテリアルテクスチャサンプラー
          .binding = 0, // バインディング位置 (`layout(binding = 0) uniform
                        // ...`の0と一致)
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
          .descriptorCount = 1, // 配列の要素数（descriptor indexing）
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .pImmutableSamplers = nullptr,
      },
      {
          // ! DescriptorIndex
          // マテリアルテクスチャ（descriptor indexingを使う）
          .binding = 1, // バインディング位置 (`layout(binding = 0) uniform
                        // ...`の0と一致)
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .descriptorCount =
              MAX_TEXTURES, // 配列の要素数（descriptor indexing）
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .pImmutableSamplers = nullptr,
      },
  };

  // ! DescriptorIndex
  VkDescriptorBindingFlags flags =
      VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
      VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

  // DescriptorSetには、Sampler, Imageの2つが含まれている。
  // そして、descriptor indexingのフラグを指定したいのは最後のImageだけである。
  std::vector<VkDescriptorBindingFlags> descriptorBindingFlags = {0, flags};
  VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
  bindingFlagsInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
  bindingFlagsInfo.bindingCount = descriptorBindingFlags.size();
  bindingFlagsInfo.pBindingFlags = descriptorBindingFlags.data();

  VkDescriptorSetLayoutCreateInfo layoutInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = &bindingFlagsInfo,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .bindingCount = static_cast<uint32_t>(uboLayoutBindings.size()),
      .pBindings = uboLayoutBindings.data(),
  };
  VK_CHECK(vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, nullptr,
                                       &m_context.textureDescriptorSetLayout));
}

void Engine::allocateTextureDescriptorSet() {
  // ! ここで指定するテクスチャ数は、最大のテクスチャ数となる。
  uint32_t variableCounts = MAX_TEXTURES;
  VkDescriptorSetVariableDescriptorCountAllocateInfo variable_info{};
  variable_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
  variable_info.descriptorSetCount = 1;
  variable_info.pDescriptorCounts = &variableCounts;

  VkDescriptorSetAllocateInfo allocInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = &variable_info,
      .descriptorPool = m_context.descriptorPool,
      .descriptorSetCount = 1,
      .pSetLayouts = &m_context.textureDescriptorSetLayout,
  };
  std::vector<VkDescriptorSet> descriptorSets(m_context.swapchain.image_count,
                                              VK_NULL_HANDLE);
  VK_CHECK(vkAllocateDescriptorSets(m_context.device, &allocInfo,
                                    &m_context.textureDescriptorSet));
}

void Engine::bindTextureDescriptorSet() {
  std::vector<VkWriteDescriptorSet> descriptorWrites;

  VkDescriptorImageInfo samplerInfos{};
  samplerInfos.sampler = m_context.textureSampler;
  samplerInfos.imageView = VK_NULL_HANDLE;              // Samplerなので不要
  samplerInfos.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Samplerなので不要

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = m_context.textureDescriptorSet; // 書き込み対象のDescriptor Set
  write.dstBinding = 0;                          // sampler用のbinding
  write.dstArrayElement = 0;                     // 配列の先頭から書き込む
  write.descriptorCount = 1;                     // samplersの数
  write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  write.pImageInfo = &samplerInfos; // VkDescriptorImageInfoの配列
  write.pBufferInfo = nullptr;
  write.pTexelBufferView = nullptr;

  descriptorWrites.push_back(write);

  // imageInfoがスコープを抜けると開放されてしまうので、ここに格納する
  std::vector<VkDescriptorImageInfo> imageInfos(m_nodes.size());
  for (size_t i = 0; i < m_nodes.size(); ++i) {
    const auto &texture = m_nodes[i]->texture();
    assert(texture != nullptr);
    const auto &textureData = m_context.textureMap[texture];
    assert(textureData.imageView != VK_NULL_HANDLE);

    VkDescriptorImageInfo img{};
    img.sampler = VK_NULL_HANDLE;
    img.imageView = textureData.imageView;
    img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[i] = img;

    VkWriteDescriptorSet imgWrite{};
    imgWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    imgWrite.dstSet = m_context.textureDescriptorSet;
    imgWrite.dstBinding = 1;
    imgWrite.dstArrayElement = static_cast<uint32_t>(i);
    imgWrite.descriptorCount = 1;
    imgWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    imgWrite.pImageInfo = &imageInfos[i];

    descriptorWrites.push_back(imgWrite);
  }
  // シーンのディスクリプタセットの更新
  vkUpdateDescriptorSets(m_context.device,
                         static_cast<uint32_t>(descriptorWrites.size()),
                         descriptorWrites.data(), 0, nullptr);
}

size_t Engine::minDynamicUBOAlignment(size_t uboSize) {
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(m_context.physicalDevice, &properties);

  size_t min_ubo_alignment = properties.limits.minUniformBufferOffsetAlignment;
  std::cout << "min_ubo_alignment = " << min_ubo_alignment << std::endl;

  size_t uboBufferSizePerNode = uboSize;
  if (min_ubo_alignment > 0) {
    uboBufferSizePerNode = (uboBufferSizePerNode + min_ubo_alignment - 1) &
                           ~(min_ubo_alignment - 1);
  }
  return uboBufferSizePerNode;
}

static const glm::mat4 bias(0.5, 0.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 0.0,
                            1.0, 0.0, 0.5, 0.5, 0.0, 1.0);

/**
 * UBOの更新
 */
void Engine::updateUBO(PerFrame &per_frame) {
  // ***** シャドウ *****
  ShadowUniformBufferObject shadowUBO{};
  glm::vec3 lightPos = m_lightPos;
  auto shadowView = glm::lookAt(lightPos, {0.f, 0.f, 0.f}, {0.f, 0.f, 1.f});
  auto shadowProj =
      glm::perspective(glm::radians(60.0f), // fov
                       static_cast<float>(m_context.swapchain.extent.width) /
                           m_context.swapchain.extent.height, // aspect ratio
                       0.1f,                                  // near
                       10.0f                                  // far
      );
  shadowProj[1][1] *= -1;
  auto shadowVP = shadowProj * shadowView;

  // ***** シーン *****
  SceneUBO_VS sceneUBOVS{};
  auto view = m_camera.getCameraView();
  auto proj =
      glm::perspective(glm::radians(60.0f), // fov
                       static_cast<float>(m_context.swapchain.extent.width) /
                           m_context.swapchain.extent.height, // aspect ratio
                       0.1f,                                  // near
                       10.0f                                  // far
      );
  // Vulkan は NDC（正規化デバイス座標）の Y が上下反転しているため、
  // GLM のプロジェクション行列をそのまま使うと上下が逆さまになる。
  // proj[1][1] *= -1; により Y 軸を反転し、Vulkan 仕様に合わせている。
  proj[1][1] *= -1;
  auto sceneVP = proj * view;
  sceneUBOVS.view = view;
  sceneUBOVS.proj = proj;
  sceneUBOVS.lightPos = m_lightPos;
  VK_CHECK(vmaCopyMemoryToAllocation(m_context.vmaAllocator, &sceneUBOVS,
                                     per_frame.sceneUniformBufferAllocation, 0,
                                     sizeof(SceneUBO_VS)));

  SceneUBO_FS sceneUBOFS{};
  sceneUBOFS.lightColor = m_lightColor;
  sceneUBOFS.intensity = m_intensity;
  sceneUBOFS.ambient = m_ambient;
  VK_CHECK(vmaCopyMemoryToAllocation(m_context.vmaAllocator, &sceneUBOFS,
                                     per_frame.sceneUniformBufferAllocation,
                                     m_context.sceneUBOBufferSizeForVS,
                                     sizeof(SceneUBO_FS)));

  for (size_t i = 0; i < m_nodes.size(); ++i) {
    auto model = m_nodes[i]->worldMatrix();

    shadowUBO.depthMVP = shadowVP * model;

    // シャドウUBOの更新
    VkDeviceSize shadowOffset = i * m_context.shadowUBOBufferSizePerNode;
    VK_CHECK(vmaCopyMemoryToAllocation(m_context.vmaAllocator, &shadowUBO,
                                       per_frame.shadowUniformBufferAllocation,
                                       shadowOffset, sizeof(shadowUBO)));
    // frustum culling
    auto shadowFrustum = extractFrustum(shadowVP);
    m_shadowCastingNodes[i] =
        sphereInFrustum(shadowFrustum, m_nodes[i]->boundingSphere());

    ModelUBO modelUBO{};
    modelUBO.shadowMatrix = bias * shadowUBO.depthMVP;
    modelUBO.model = model;
    modelUBO.texIndex = static_cast<uint32_t>(i);

    // モデルUBOの更新
    VkDeviceSize offset = i * m_context.modelUBOBufferSizePerNode;
    VK_CHECK(vmaCopyMemoryToAllocation(m_context.vmaAllocator, &modelUBO,
                                       per_frame.modelUniformBufferAllocation,
                                       offset, sizeof(modelUBO)));
    // frustum culling
    auto sceneFrustum = extractFrustum(sceneVP);
    m_visibleNodes[i] =
        sphereInFrustum(sceneFrustum, m_nodes[i]->boundingSphere());
  }
}

void Engine::initPerFrame(PerFrame &per_frame) {
  VkFenceCreateInfo info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                         .flags = VK_FENCE_CREATE_SIGNALED_BIT};
  VK_CHECK(vkCreateFence(m_context.device, &info, nullptr,
                         &per_frame.queue_submit_fence));

  VkCommandPoolCreateInfo cmd_pool_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = static_cast<uint32_t>(m_context.graphicsQueueIndex)};
  VK_CHECK(vkCreateCommandPool(m_context.device, &cmd_pool_info, nullptr,
                               &per_frame.primary_command_pool));

  VkCommandBufferAllocateInfo cmd_buf_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = per_frame.primary_command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  VK_CHECK(vkAllocateCommandBuffers(m_context.device, &cmd_buf_info,
                                    &per_frame.primary_command_buffer));
  LOGD("init_per_frame: primary_command_buffer = {:x}",
       reinterpret_cast<uint64_t>(per_frame.primary_command_buffer));
}

void Engine::teardownPerFrame(PerFrame &per_frame) {
  if (per_frame.queue_submit_fence != VK_NULL_HANDLE) {
    vkDestroyFence(m_context.device, per_frame.queue_submit_fence, nullptr);

    per_frame.queue_submit_fence = VK_NULL_HANDLE;
  }

  if (per_frame.primary_command_buffer != VK_NULL_HANDLE) {
    vkFreeCommandBuffers(m_context.device, per_frame.primary_command_pool, 1,
                         &per_frame.primary_command_buffer);

    per_frame.primary_command_buffer = VK_NULL_HANDLE;
  }

  if (per_frame.primary_command_pool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(m_context.device, per_frame.primary_command_pool,
                         nullptr);

    per_frame.primary_command_pool = VK_NULL_HANDLE;
  }

  if (per_frame.swapchain_acquire_semaphore != VK_NULL_HANDLE) {
    vkDestroySemaphore(m_context.device, per_frame.swapchain_acquire_semaphore,
                       nullptr);

    per_frame.swapchain_acquire_semaphore = VK_NULL_HANDLE;
  }

  if (per_frame.swapchain_release_semaphore != VK_NULL_HANDLE) {
    vkDestroySemaphore(m_context.device, per_frame.swapchain_release_semaphore,
                       nullptr);

    per_frame.swapchain_release_semaphore = VK_NULL_HANDLE;
  }

  if (per_frame.sceneUniformBuffer != VK_NULL_HANDLE) {
    vmaDestroyBuffer(m_context.vmaAllocator, per_frame.sceneUniformBuffer,
                     per_frame.sceneUniformBufferAllocation);
    per_frame.sceneUniformBuffer = VK_NULL_HANDLE;
    per_frame.sceneUniformBufferAllocation = VK_NULL_HANDLE;
  }

  if (per_frame.modelUniformBuffer != VK_NULL_HANDLE) {
    vmaDestroyBuffer(m_context.vmaAllocator, per_frame.modelUniformBuffer,
                     per_frame.modelUniformBufferAllocation);
    per_frame.modelUniformBuffer = VK_NULL_HANDLE;
    per_frame.modelUniformBufferAllocation = VK_NULL_HANDLE;
  }

  if (per_frame.shadowUniformBuffer != VK_NULL_HANDLE) {
    vmaDestroyBuffer(m_context.vmaAllocator, per_frame.shadowUniformBuffer,
                     per_frame.shadowUniformBufferAllocation);
    per_frame.shadowUniformBuffer = VK_NULL_HANDLE;
    per_frame.shadowUniformBufferAllocation = VK_NULL_HANDLE;
  }
}

void Engine::initSwapchain() {
  vkb::SwapchainBuilder swapchain_builder{m_context.device};
  swapchain_builder.set_desired_min_image_count(
      vkb::SwapchainBuilder::DOUBLE_BUFFERING);
  auto swap_ret =
      swapchain_builder.set_old_swapchain(m_context.swapchain).build();
  if (!swap_ret) {
    LOGE("failed to create swapchain");
    return;
  }
  vkb::destroy_swapchain(m_context.swapchain);
  m_context.swapchain = swap_ret.value();
  uint32_t image_count = m_context.swapchain.image_count;
  std::cout << "image_count = " << image_count << std::endl;

  m_context.swapchainDimensions = {m_context.swapchain.extent.width,
                                   m_context.swapchain.extent.height,
                                   m_context.swapchain.image_format};

  m_context.swapchainImages = m_context.swapchain.get_images().value();

  m_context.perFrame.clear();
  m_context.perFrame.resize(image_count);
  for (size_t i = 0; i < image_count; i++) {
    initPerFrame(m_context.perFrame[i]);
  }

  m_context.swapchainImageViews = m_context.swapchain.get_image_views().value();
}

static std::vector<char> readFile(const std::string &filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    throw std::runtime_error("failed to open file!");
  }

  size_t fileSize = (size_t)file.tellg();
  std::vector<char> buffer(fileSize);

  file.seekg(0);
  file.read(buffer.data(), fileSize);

  file.close();

  return buffer;
}

VkShaderModule Engine::loadShaderModule(const char *path) {
  std::vector<char> spirv = readFile(path);
  VkShaderModuleCreateInfo module_info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spirv.size(),
      .pCode = reinterpret_cast<const uint32_t *>(spirv.data())};

  VkShaderModule shader_module;
  VK_CHECK(vkCreateShaderModule(m_context.device, &module_info, nullptr,
                                &shader_module));

  return shader_module;
}

void Engine::initPipeline() {
  std::vector<VkDescriptorSetLayout> layouts = {
      m_context.sceneDescriptorSetLayout,
      m_context.modelDescriptorSetLayout,
      m_context.textureDescriptorSetLayout,
  };
  VkPipelineLayoutCreateInfo layout_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data()};
  VK_CHECK(vkCreatePipelineLayout(m_context.device, &layout_info, nullptr,
                                  &m_context.pipelineLayout));

  VkVertexInputBindingDescription binding_description{
      .binding = 0,
      .stride = sizeof(Vertex),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};

  std::vector<VkVertexInputAttributeDescription> attribute_descriptions = {{
      {.location = 0,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32_SFLOAT,
       .offset = offsetof(Vertex, position)}, // position
      {.location = 1,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32_SFLOAT,
       .offset = offsetof(Vertex, normal)}, // normal
      {.location = 2,
       .binding = 0,
       .format = VK_FORMAT_R32G32_SFLOAT,
       .offset = offsetof(Vertex, texCoord)}, // texCoord
  }};

  VkPipelineVertexInputStateCreateInfo vertex_input{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &binding_description,
      .vertexAttributeDescriptionCount =
          static_cast<uint32_t>(attribute_descriptions.size()),
      .pVertexAttributeDescriptions = attribute_descriptions.data()};

  VkPipelineInputAssemblyStateCreateInfo input_assembly{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE};

  VkPipelineRasterizationStateCreateInfo raster{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .depthBiasEnable = VK_FALSE,
      .lineWidth = 1.0f};

  std::vector<VkDynamicState> dynamic_states = {
      VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE,
      VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY};

  VkPipelineColorBlendAttachmentState blend_attachment{
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};

  VkPipelineColorBlendStateCreateInfo blend{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &blend_attachment};

  VkPipelineViewportStateCreateInfo viewport{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1};

  VkPipelineDepthStencilStateCreateInfo depth_stencil{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable = VK_FALSE,
      .minDepthBounds = 0.0f,
      .maxDepthBounds = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = m_msaaSamples,
      .sampleShadingEnable = VK_FALSE,
  };

  VkPipelineDynamicStateCreateInfo dynamic_state_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
      .pDynamicStates = dynamic_states.data()};

  std::vector<VkPipelineShaderStageCreateInfo> shader_stages = {
      {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = loadShaderModule("shaders/scene.vert.spv"),
        .pName = "main"},
       {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = loadShaderModule("shaders/scene.frag.spv"),
        .pName = "main"}}};

  VkPipelineRenderingCreateInfo pipeline_rendering_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &m_context.swapchainDimensions.format,
      .depthAttachmentFormat = m_context.depthFormat};

  VkGraphicsPipelineCreateInfo pipe{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &pipeline_rendering_info,
      .stageCount = static_cast<uint32_t>(shader_stages.size()),
      .pStages = shader_stages.data(),
      .pVertexInputState = &vertex_input,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pDepthStencilState = &depth_stencil,
      .pColorBlendState = &blend,
      .pDynamicState = &dynamic_state_info,
      .layout = m_context.pipelineLayout,
      .renderPass = VK_NULL_HANDLE,
      .subpass = 0,
  };

  VK_CHECK(vkCreateGraphicsPipelines(m_context.device, VK_NULL_HANDLE, 1, &pipe,
                                     nullptr, &m_context.pipeline));

  vkDestroyShaderModule(m_context.device, shader_stages[0].module, nullptr);
  vkDestroyShaderModule(m_context.device, shader_stages[1].module, nullptr);
}

void Engine::initShadowPipeline() {
  VkPipelineLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &m_context.shadowDescriptorSetLayout;
  VK_CHECK(vkCreatePipelineLayout(m_context.device, &layout_info, nullptr,
                                  &m_context.shadowPipelineLayout));

  VkVertexInputBindingDescription binding_description{
      .binding = 0,
      .stride = sizeof(Vertex),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};

  std::vector<VkVertexInputAttributeDescription> attribute_descriptions = {{
      {.location = 0,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32_SFLOAT,
       .offset = offsetof(Vertex, position)}, // position
  }};

  VkPipelineVertexInputStateCreateInfo vertex_input{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &binding_description,
      .vertexAttributeDescriptionCount =
          static_cast<uint32_t>(attribute_descriptions.size()),
      .pVertexAttributeDescriptions = attribute_descriptions.data()};

  VkPipelineInputAssemblyStateCreateInfo input_assembly{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE};

  VkPipelineRasterizationStateCreateInfo raster{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .depthBiasEnable = VK_FALSE,
      .lineWidth = 1.0f};

  std::vector<VkDynamicState> dynamic_states = {
      VK_DYNAMIC_STATE_VIEWPORT,           VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_CULL_MODE,          VK_DYNAMIC_STATE_FRONT_FACE,
      VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, VK_DYNAMIC_STATE_DEPTH_BIAS // shadow
  };

  VkPipelineColorBlendAttachmentState blend_attachment{
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};

  VkPipelineColorBlendStateCreateInfo blend{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &blend_attachment};

  VkPipelineViewportStateCreateInfo viewport{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1};

  VkPipelineDepthStencilStateCreateInfo depth_stencil{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable = VK_FALSE,
      .minDepthBounds = 0.0f,
      .maxDepthBounds = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable = VK_FALSE,
  };

  VkPipelineDynamicStateCreateInfo dynamic_state_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
      .pDynamicStates = dynamic_states.data()};

  std::vector<VkPipelineShaderStageCreateInfo> shader_stages = {
      {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = loadShaderModule("shaders/shadow.vert.spv"),
        .pName = "main"},
       {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = loadShaderModule("shaders/shadow.frag.spv"),
        .pName = "main"}}};

  VkPipelineRenderingCreateInfo pipeline_rendering_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 0,
      .depthAttachmentFormat = m_context.shadowDepthFormat};

  VkGraphicsPipelineCreateInfo pipe{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &pipeline_rendering_info,
      .stageCount = static_cast<uint32_t>(shader_stages.size()),
      .pStages = shader_stages.data(),
      .pVertexInputState = &vertex_input,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pDepthStencilState = &depth_stencil,
      .pColorBlendState = &blend,
      .pDynamicState = &dynamic_state_info,
      .layout = m_context.shadowPipelineLayout,
      .renderPass = VK_NULL_HANDLE,
      .subpass = 0,
  };

  VK_CHECK(vkCreateGraphicsPipelines(m_context.device, VK_NULL_HANDLE, 1, &pipe,
                                     nullptr, &m_context.shadowPipeline));

  vkDestroyShaderModule(m_context.device, shader_stages[0].module, nullptr);
  vkDestroyShaderModule(m_context.device, shader_stages[1].module, nullptr);
}

VkResult Engine::acquireNextSwapchainImage(uint32_t *image) {
  VkSemaphore acquire_semaphore;
  if (m_context.recycledSemaphores.empty()) {
    VkSemaphoreCreateInfo info = {.sType =
                                      VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK(vkCreateSemaphore(m_context.device, &info, nullptr,
                               &acquire_semaphore));
  } else {
    acquire_semaphore = m_context.recycledSemaphores.back();
    m_context.recycledSemaphores.pop_back();
  }

  VkResult res =
      vkAcquireNextImageKHR(m_context.device, m_context.swapchain, UINT64_MAX,
                            acquire_semaphore, VK_NULL_HANDLE, image);

  if (res != VK_SUCCESS) {
    m_context.recycledSemaphores.push_back(acquire_semaphore);
    return res;
  }

  if (m_context.perFrame[*image].queue_submit_fence != VK_NULL_HANDLE) {
    vkWaitForFences(m_context.device, 1,
                    &m_context.perFrame[*image].queue_submit_fence, true,
                    UINT64_MAX);
    vkResetFences(m_context.device, 1,
                  &m_context.perFrame[*image].queue_submit_fence);
  }

  if (m_context.perFrame[*image].primary_command_pool != VK_NULL_HANDLE) {
    vkResetCommandPool(m_context.device,
                       m_context.perFrame[*image].primary_command_pool, 0);
  }

  VkSemaphore old_semaphore =
      m_context.perFrame[*image].swapchain_acquire_semaphore;

  if (old_semaphore != VK_NULL_HANDLE) {
    m_context.recycledSemaphores.push_back(old_semaphore);
  }

  m_context.perFrame[*image].swapchain_acquire_semaphore = acquire_semaphore;

  return VK_SUCCESS;
}

void Engine::renderShadow(uint32_t swapchain_index, VkCommandBuffer cmd) {
  VkClearValue shadowClearValue = {
      .depthStencil = {.depth = 1.0f, .stencil = 0}};
  VkRenderingAttachmentInfo depth_attachment{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = m_context.shadowImageView,
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = shadowClearValue};

  VkRenderingInfo rendering_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
      .renderArea = {.offset = {0, 0},
                     .extent = {.width = SHADOWMAP_SIZE,
                                .height = SHADOWMAP_SIZE}},
      .layerCount = 1,
      .colorAttachmentCount = 0,
      .pColorAttachments = nullptr,
      .pDepthAttachment = &depth_attachment};

  vkCmdBeginRendering(cmd, &rendering_info);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_context.shadowPipeline);

  VkViewport vp{.width = Engine::SHADOWMAP_SIZE,
                .height = Engine::SHADOWMAP_SIZE,
                .minDepth = 0.0f,
                .maxDepth = 1.0f};

  vkCmdSetViewport(cmd, 0, 1, &vp);

  VkRect2D scissor{.extent = {.width = Engine::SHADOWMAP_SIZE,
                              .height = Engine::SHADOWMAP_SIZE}};

  vkCmdSetScissor(cmd, 0, 1, &scissor);

  vkCmdSetCullMode(cmd, VK_CULL_MODE_FRONT_BIT);

  vkCmdSetFrontFace(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);

  vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  vkCmdSetDepthBias(cmd, Engine::depthBiasConstant, 0.0f,
                    Engine::depthBiasSlope);

  for (std::size_t i = 0; i < m_nodes.size(); ++i) {
    if (!m_shadowCastingNodes[i]) {
      continue;
    }
    const auto &node = m_nodes[i];
    const auto &meshBuffer = m_context.meshBufferMap[node->mesh()];
    const auto &vertexBuffer = meshBuffer.vertexBuffer;
    VkDeviceSize offset = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.buffer, &offset);
    const auto &indexBuffer = meshBuffer.indexBuffer;
    vkCmdBindIndexBuffer(cmd, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    uint32_t dynamic_offset =
        static_cast<uint32_t>(i * m_context.shadowUBOBufferSizePerNode);
    vkCmdBindDescriptorSets(
        cmd,                             // commandBuffer
        VK_PIPELINE_BIND_POINT_GRAPHICS, // pipelineBindPoint
        m_context.shadowPipelineLayout,  // layout
        0,                               // firstSet
        1,                               // descriptorSetCount
        &m_context.perFrame[swapchain_index]
             .shadowDescriptorSet, // pDescriptorSets
        1,                         // dynamicOffsetCount
        &dynamic_offset            // pDynamicOffsets
    );
    vkCmdDrawIndexed(cmd,
                     static_cast<uint32_t>(node->mesh()->numberOfIndices()), 1,
                     0, 0, 0);
  }
  vkCmdEndRendering(cmd);
}

void Engine::render(uint32_t swapchain_index) {
  VkCommandBuffer cmd =
      m_context.perFrame[swapchain_index].primary_command_buffer;

  VkCommandBufferBeginInfo begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};

  VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));

  // MARK: Shadow Rendering
  {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = m_context.shadowImage,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, // 深度専用の場合
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkDependencyInfo depInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmd, &depInfo);
  }

  renderShadow(swapchain_index, cmd);

  // 例: depthImage を DEPTH_STENCIL_ATTACHMENT_OPTIMAL ->
  // SHADER_READ_ONLY_OPTIMAL に遷移
  VkImageMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .pNext = NULL,
      .srcStageMask =
          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, // depth write
                                                       // が完了するステージ
      .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      .dstStageMask =
          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, // フラグメントでサンプリングするステージ
      .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = m_context.shadowImage,
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, // 深度単独の場合
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };

  VkDependencyInfo depInfo = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .pNext = NULL,
      .memoryBarrierCount = 0,
      .pMemoryBarriers = NULL,
      .bufferMemoryBarrierCount = 0,
      .pBufferMemoryBarriers = NULL,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrier,
  };

  vkCmdPipelineBarrier2(cmd, &depInfo);

  // MARK: Scene Rendering

  transitionImageLayout(
      cmd, m_context.colorImages[swapchain_index], VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_NONE,
      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

  transitionImageLayout(
      cmd, m_context.swapchainImages[swapchain_index],
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
  VkClearValue clear_value{.color = {{0.01f, 0.01f, 0.033f, 1.0f}}};

  VkRenderingAttachmentInfo color_attachment{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = m_context.colorImageViews[swapchain_index],
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
      .resolveImageView = m_context.swapchainImageViews[swapchain_index],
      .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = clear_value,
  };

  VkClearValue depthClearValue = {
      .depthStencil = {.depth = 1.0f, .stencil = 0}};
  VkRenderingAttachmentInfo depth_attachment{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = m_context.depthImageView,
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .clearValue = depthClearValue};

  VkRenderingInfo rendering_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
      .renderArea = {.offset = {0, 0},
                     .extent = {.width = m_context.swapchainDimensions.width,
                                .height =
                                    m_context.swapchainDimensions.height}},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment,
      .pDepthAttachment = &depth_attachment};

  vkCmdBeginRendering(cmd, &rendering_info);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_context.pipeline);

  VkViewport vp{
      .width = static_cast<float>(m_context.swapchainDimensions.width),
      .height = static_cast<float>(m_context.swapchainDimensions.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f};

  vkCmdSetViewport(cmd, 0, 1, &vp);

  VkRect2D scissor{.extent = {.width = m_context.swapchainDimensions.width,
                              .height = m_context.swapchainDimensions.height}};

  vkCmdSetScissor(cmd, 0, 1, &scissor);

  vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);

  vkCmdSetFrontFace(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);

  vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  vkCmdBindDescriptorSets(
      cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_context.pipelineLayout,
      0, // first set
      1, // descriptorSetCount
      &m_context.perFrame[swapchain_index].sceneDescriptorSet, 0, nullptr);

  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_context.pipelineLayout,
                          2, // first set
                          1, // descriptorSetCount
                          &m_context.textureDescriptorSet, 0, nullptr);

  for (std::size_t i = 0; i < m_nodes.size(); ++i) {
    if (!m_visibleNodes[i]) {
      continue;
    }
    const auto &node = m_nodes[i];
    const auto &meshBuffer = m_context.meshBufferMap[node->mesh()];
    const auto &vertexBuffer = meshBuffer.vertexBuffer;
    VkDeviceSize offset = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.buffer, &offset);
    const auto &indexBuffer = meshBuffer.indexBuffer;
    vkCmdBindIndexBuffer(cmd, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    uint32_t dynamic_offset =
        static_cast<uint32_t>(i * m_context.modelUBOBufferSizePerNode);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_context.pipelineLayout,
        1, // first set
        1, // descriptorSetCount
        &m_context.perFrame[swapchain_index].modelDescriptorSet, 1,
        &dynamic_offset);
    vkCmdDrawIndexed(cmd,
                     static_cast<uint32_t>(node->mesh()->numberOfIndices()), 1,
                     0, 0, 0);
  }

  vkCmdEndRendering(cmd);

  transitionImageLayout(
      cmd, m_context.swapchainImages[swapchain_index],
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,          // srcAccessMask
      VK_ACCESS_2_MEMORY_READ_BIT,                     // dstAccessMask
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, // srcStage
      VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT           // dstStage
  );

  {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask =
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, // 前のパスでサンプリングしていた
        .srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .dstStageMask =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, // 次のパスで深度として使う
        .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = m_context.shadowImage,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkDependencyInfo depInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmd, &depInfo);
  }

  VK_CHECK(vkEndCommandBuffer(cmd));

  if (m_context.perFrame[swapchain_index].swapchain_release_semaphore ==
      VK_NULL_HANDLE) {
    VkSemaphoreCreateInfo semaphore_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK(vkCreateSemaphore(
        m_context.device, &semaphore_info, nullptr,
        &m_context.perFrame[swapchain_index].swapchain_release_semaphore));
  }

  VkPipelineStageFlags wait_stage{VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};

  VkSubmitInfo info{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores =
          &m_context.perFrame[swapchain_index].swapchain_acquire_semaphore,
      .pWaitDstStageMask = &wait_stage,
      .commandBufferCount = 1,
      .pCommandBuffers = &cmd,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores =
          &m_context.perFrame[swapchain_index].swapchain_release_semaphore};

  VK_CHECK(
      vkQueueSubmit(m_context.queue, 1, &info,
                    m_context.perFrame[swapchain_index].queue_submit_fence));
}

VkResult Engine::presentImage(uint32_t index) {
  VkSwapchainKHR swapChains[] = {m_context.swapchain};
  VkPresentInfoKHR present{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &m_context.perFrame[index].swapchain_release_semaphore,
      .swapchainCount = 1,
      .pSwapchains = swapChains,
      .pImageIndices = &index,
  };
  return vkQueuePresentKHR(m_context.queue, &present);
}

void Engine::transitionImageLayout(VkCommandBuffer cmd, VkImage image,
                                   VkImageLayout oldLayout,
                                   VkImageLayout newLayout,
                                   VkAccessFlags2 srcAccessMask,
                                   VkAccessFlags2 dstAccessMask,
                                   VkPipelineStageFlags2 srcStage,
                                   VkPipelineStageFlags2 dstStage) {
  VkImageMemoryBarrier2 image_barrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = srcStage,
      .srcAccessMask = srcAccessMask,
      .dstStageMask = dstStage,
      .dstAccessMask = dstAccessMask,

      .oldLayout = oldLayout,
      .newLayout = newLayout,

      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

      .image = image,

      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1}};

  VkDependencyInfo dependency_info{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                   .dependencyFlags = 0,
                                   .imageMemoryBarrierCount = 1,
                                   .pImageMemoryBarriers = &image_barrier};
  vkCmdPipelineBarrier2(cmd, &dependency_info);
}

void Engine::transitionImageLayout(VkImage image, VkFormat format,
                                   VkImageLayout oldLayout,
                                   VkImageLayout newLayout) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    throw std::invalid_argument("unsupported layout transition!");
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);

  endSingleTimeCommands(commandBuffer);
}

Engine::~Engine() {
  if (m_context.device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_context.device);
  }

  for (auto &per_frame : m_context.perFrame) {
    teardownPerFrame(per_frame);
  }

  m_context.perFrame.clear();

  for (auto semaphore : m_context.recycledSemaphores) {
    vkDestroySemaphore(m_context.device, semaphore, nullptr);
  }

  if (m_context.pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(m_context.device, m_context.pipeline, nullptr);
  }

  if (m_context.pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(m_context.device, m_context.pipelineLayout,
                            nullptr);
  }

  for (VkImageView image_view : m_context.swapchainImageViews) {
    vkDestroyImageView(m_context.device, image_view, nullptr);
  }

  vkb::destroy_swapchain(m_context.swapchain);

  if (m_context.surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(m_context.instance, m_context.surface, nullptr);
    m_context.surface = VK_NULL_HANDLE;
  }

  if (m_context.sceneDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(m_context.device,
                                 m_context.sceneDescriptorSetLayout, nullptr);
    m_context.sceneDescriptorSetLayout = VK_NULL_HANDLE;
  }
  if (m_context.modelDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(m_context.device,
                                 m_context.modelDescriptorSetLayout, nullptr);
    m_context.modelDescriptorSetLayout = VK_NULL_HANDLE;
  }
  if (m_context.textureDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(m_context.device,
                                 m_context.textureDescriptorSetLayout, nullptr);
    m_context.textureDescriptorSetLayout = VK_NULL_HANDLE;
  }
  if (m_context.descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(m_context.device, m_context.descriptorPool,
                            nullptr);
    m_context.descriptorPool = VK_NULL_HANDLE;
  }

  for (size_t i = 0; i < m_context.colorImages.size(); ++i) {
    vkDestroyImageView(m_context.device, m_context.colorImageViews[i], nullptr);
    vmaDestroyImage(m_context.vmaAllocator, m_context.colorImages[i],
                    m_context.colorAllocations[i]);
  }
  vkDestroyImageView(m_context.device, m_context.depthImageView, nullptr);
  vmaDestroyImage(m_context.vmaAllocator, m_context.depthImage,
                  m_context.depthAllocation);

  vkDestroyImageView(m_context.device, m_context.shadowImageView, nullptr);
  vmaDestroyImage(m_context.vmaAllocator, m_context.shadowImage,
                  m_context.shadowAllocation);
  vkDestroySampler(m_context.device, m_context.shadowSampler, nullptr);

  vkDestroyPipelineLayout(m_context.device, m_context.shadowPipelineLayout,
                          nullptr);
  vkDestroyPipeline(m_context.device, m_context.shadowPipeline, nullptr);
  vkDestroyDescriptorSetLayout(m_context.device,
                               m_context.shadowDescriptorSetLayout, nullptr);

  for (auto &pair : m_context.meshBufferMap) {
    vmaDestroyBuffer(m_context.vmaAllocator, pair.second.vertexBuffer.buffer,
                     pair.second.vertexBuffer.allocation);
    vmaDestroyBuffer(m_context.vmaAllocator, pair.second.indexBuffer.buffer,
                     pair.second.indexBuffer.allocation);
  }

  for (auto &pair : m_context.textureMap) {
    vkDestroyImageView(m_context.device, pair.second.imageView, nullptr);
    vmaDestroyImage(m_context.vmaAllocator, pair.second.image,
                    pair.second.allocation);
  }
  vkDestroySampler(m_context.device, m_context.textureSampler, nullptr);

  vmaDestroyAllocator(m_context.vmaAllocator);

  if (m_context.commandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(m_context.device, m_context.commandPool, nullptr);
  }

  if (m_context.device != VK_NULL_HANDLE) {
    vkb::destroy_device(m_context.device);
  }

  SDL_DestroyWindow(m_context.window);
  SDL_Quit();
}

VkFormat Engine::findSupportedFormat(const std::vector<VkFormat> &candidates,
                                     VkImageTiling tiling,
                                     VkFormatFeatureFlags features) {
  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, format,
                                        &props);

    if (tiling == VK_IMAGE_TILING_LINEAR &&
        (props.linearTilingFeatures & features) == features) {
      return format;
    } else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
               (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }

  throw std::runtime_error("failed to find supported format!");
}

VkFormat Engine::findDepthFormat() {
  return findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
       VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void Engine::initColor() {
  auto n = m_context.swapchain.image_count;
  m_context.colorImages.resize(n);
  m_context.colorAllocations.resize(n);
  m_context.colorImageViews.resize(n);
  for (size_t i = 0; i < m_context.swapchain.image_count; ++i) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {m_context.swapchainDimensions.width,
                        m_context.swapchainDimensions.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_context.swapchain.image_format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.samples = m_msaaSamples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(m_context.vmaAllocator, &imageInfo,
                            &allocCreateInfo, &m_context.colorImages[i],
                            &m_context.colorAllocations[i], nullptr));
    LOGD("context.colorImage = {:x}",
         reinterpret_cast<uint64_t>(m_context.colorImages[i]));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_context.colorImages[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_context.swapchain.image_format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_context.device, &viewInfo, nullptr,
                          &m_context.colorImageViews[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create image view!");
    }
    LOGD("context.colorImageView = {:x}",
         reinterpret_cast<uint64_t>(m_context.colorImageViews[i]));
  }
}

void Engine::initDepth() {
  m_context.depthFormat = findDepthFormat();

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {m_context.swapchainDimensions.width,
                      m_context.swapchainDimensions.height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = m_context.depthFormat;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  imageInfo.samples = m_msaaSamples;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocCreateInfo{};
  allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  VK_CHECK(vmaCreateImage(m_context.vmaAllocator, &imageInfo, &allocCreateInfo,
                          &m_context.depthImage, &m_context.depthAllocation,
                          nullptr));
  LOGD("context.depthImage = {:x}",
       reinterpret_cast<uint64_t>(m_context.depthImage));

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_context.depthImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = m_context.depthFormat;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VK_CHECK(vkCreateImageView(m_context.device, &viewInfo, nullptr,
                             &m_context.depthImageView));
  LOGD("context.depthImageView = {:x}",
       reinterpret_cast<uint64_t>(m_context.depthImageView));
}

void Engine::initShadow() {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {SHADOWMAP_SIZE, SHADOWMAP_SIZE, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = m_context.shadowDepthFormat;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage =
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocCreateInfo{};
  allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  VK_CHECK(vmaCreateImage(m_context.vmaAllocator, &imageInfo, &allocCreateInfo,
                          &m_context.shadowImage, &m_context.shadowAllocation,
                          nullptr));

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_context.shadowImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = imageInfo.format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VK_CHECK(vkCreateImageView(m_context.device, &viewInfo, nullptr,
                             &m_context.shadowImageView));
}

bool Engine::prepare() {
  if (volkInitialize() != VK_SUCCESS) {
    throw std::runtime_error("failed to initialize volk");
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw std::runtime_error("failed to initialize SDL");
  }
  m_context.window =
      SDL_CreateWindow("b3Engine", m_windowWidth, m_windowHeight,
                       SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_VULKAN);
  if (m_context.window == nullptr) {
    throw std::runtime_error("failed to create window");
  }
  SDL_SetWindowRelativeMouseMode(m_context.window, true);

  initInstance();

  if (!SDL_Vulkan_CreateSurface(m_context.window, m_context.instance, nullptr,
                                &m_context.surface)) {
    throw std::runtime_error("failed to create surface");
  }

  m_context.swapchainDimensions.width = m_windowWidth;
  m_context.swapchainDimensions.height = m_windowHeight;

  if (!m_context.surface) {
    throw std::runtime_error("Failed to create window surface.");
  }

  initDevice();

  initVertexBuffer();
  initTexture();

  initSwapchain();

  initShadow();
  initUBO();

  initColor();
  initDepth();

  initPipeline();
  initShadowPipeline();

  return true;
}

void Engine::mainLoop() {
  bool running = true;
  Uint64 lastTicks = 0;
  while (running) {
    Uint64 ticks = SDL_GetTicks();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      }
      if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        running = false;
      }
      if (event.type == SDL_EVENT_WINDOW_RESIZED) {
      }
      m_camera.handleMouseEvent(event);
    }
    if (lastTicks != 0) {
      float df = static_cast<float>(ticks - lastTicks) / 1000.f;
      m_camera.updateCameraMovement(df);
    }
    update();
    lastTicks = ticks;
  }
  // 終了する前に、すべての描画完了するまで待機する
  vkDeviceWaitIdle(m_context.device);
}

void Engine::update() {
  auto res = acquireNextSwapchainImage(&m_context.currentIndex);

  if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR) {
    if (!resize(m_context.swapchainDimensions.width,
                m_context.swapchainDimensions.height)) {
      LOGI("Resize failed");
    }
    res = acquireNextSwapchainImage(&m_context.currentIndex);
  }

  if (res != VK_SUCCESS) {
    vkQueueWaitIdle(m_context.queue);
    return;
  }

  m_shadowCastingNodes.resize(m_nodes.size());
  m_visibleNodes.resize(m_nodes.size());
  updateUBO(m_context.perFrame[m_context.currentIndex]);
  render(m_context.currentIndex);
  res = presentImage(m_context.currentIndex);

  if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR) {
    if (!resize(m_context.swapchainDimensions.width,
                m_context.swapchainDimensions.height)) {
      LOGI("Resize failed");
    }
  } else if (res != VK_SUCCESS) {
    LOGE("Failed to present swapchain image.");
  }
}

bool Engine::resize(const uint32_t, const uint32_t) {
  if (m_context.device == VK_NULL_HANDLE) {
    return false;
  }

  VkSurfaceCapabilitiesKHR surface_properties;
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      m_context.physicalDevice, m_context.surface, &surface_properties));

  if (surface_properties.currentExtent.width ==
          m_context.swapchainDimensions.width &&
      surface_properties.currentExtent.height ==
          m_context.swapchainDimensions.height) {
    return false;
  }

  vkDeviceWaitIdle(m_context.device);

  initSwapchain();
  return true;
}

VkSurfaceFormatKHR
Engine::selectSurfaceFormat(VkPhysicalDevice gpu, VkSurfaceKHR surface,
                            std::vector<VkFormat> const &preferred_formats) {
  uint32_t surface_format_count;
  vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &surface_format_count,
                                       nullptr);
  assert(0 < surface_format_count);
  std::vector<VkSurfaceFormatKHR> supported_surface_formats(
      surface_format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &surface_format_count,
                                       supported_surface_formats.data());

  auto it = std::ranges::find_if(
      supported_surface_formats,
      [&preferred_formats](VkSurfaceFormatKHR surface_format) {
        return std::ranges::any_of(preferred_formats,
                                   [&surface_format](VkFormat format) {
                                     return format == surface_format.format;
                                   });
      });

  return it != supported_surface_formats.end() ? *it
                                               : supported_surface_formats[0];
}

VkCommandBuffer Engine::beginSingleTimeCommands() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = m_context.commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  VK_CHECK(
      vkAllocateCommandBuffers(m_context.device, &allocInfo, &commandBuffer));
  LOGD("beginSingleTimeCommands: commandBuffer = {:x}",
       reinterpret_cast<uint64_t>(commandBuffer));

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  return commandBuffer;
}

void Engine::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(m_context.queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(m_context.queue);

  vkFreeCommandBuffers(m_context.device, m_context.commandPool, 1,
                       &commandBuffer);
}

AllocatedBuffer Engine::createBuffer(VkDeviceSize size,
                                     VkBufferUsageFlags usage,
                                     VmaMemoryUsage memoryUsage) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocationInfo{};
  allocationInfo.usage = memoryUsage;

  VkBuffer buffer;
  VmaAllocation allocation;
  VK_CHECK(vmaCreateBuffer(m_context.vmaAllocator, &bufferInfo, &allocationInfo,
                           &buffer, &allocation, nullptr));

  return {buffer, allocation};
}

void Engine::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer,
                        VkDeviceSize size) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferCopy copyRegion{};
  copyRegion.size = size;
  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  endSingleTimeCommands(commandBuffer);
}

AllocatedBuffer Engine::uploadBuffer(const void *srcData, VkDeviceSize size,
                                     VkBufferUsageFlags usage) {
  auto staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VMA_MEMORY_USAGE_CPU_ONLY);
  VK_CHECK(vmaCopyMemoryToAllocation(m_context.vmaAllocator, srcData,
                                     staging.allocation, 0, size));
  auto gpu = createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VMA_MEMORY_USAGE_GPU_ONLY);
  copyBuffer(staging.buffer, gpu.buffer, size);
  vmaDestroyBuffer(m_context.vmaAllocator, staging.buffer, staging.allocation);
  return gpu;
}

AllocatedImage Engine::createImage(uint32_t width, uint32_t height,
                                   uint32_t mipLevels,
                                   VkSampleCountFlagBits numSamples,
                                   VkFormat format, VkImageTiling tiling,
                                   VkImageUsageFlags usage,
                                   VmaMemoryUsage memoryUsage) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = numSamples;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocInfo = {};
  allocInfo.usage = memoryUsage;
  AllocatedImage allocatedImage;
  VK_CHECK(vmaCreateImage(m_context.vmaAllocator, &imageInfo, &allocInfo,
                          &allocatedImage.image, &allocatedImage.allocation,
                          nullptr));
  return allocatedImage;
}

void Engine::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                               uint32_t height) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();
  VkImageSubresourceLayers subresource{
      // 色、深度、ステンシルなどのどのアスペクトをコピーするか
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      // コピーするミップレベル
      .mipLevel = 0,
      // コピーする配列レイヤーの最初のレイヤー
      .baseArrayLayer = 0,
      // コピーする配列レイヤーのレイヤー数
      .layerCount = 1,
  };
  VkBufferImageCopy region{.bufferOffset = 0,
                           .bufferRowLength = 0,
                           .bufferImageHeight = 0,

                           .imageSubresource = subresource,

                           .imageOffset = {0, 0, 0},
                           .imageExtent = {width, height, 1}};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  endSingleTimeCommands(commandBuffer);
}

void Engine::addNode(const std::shared_ptr<Node> &node) {
  m_nodes.push_back(node);
}

// MARK: MSAA

VkSampleCountFlagBits Engine::getMaxUsableSampleCount() {
  VkPhysicalDeviceProperties physicalDeviceProperties;
  vkGetPhysicalDeviceProperties(m_context.physicalDevice,
                                &physicalDeviceProperties);

  VkSampleCountFlags counts =
      physicalDeviceProperties.limits.framebufferColorSampleCounts &
      physicalDeviceProperties.limits.framebufferDepthSampleCounts;
  // MSAAx4までしか用いないこととする。
  if (counts & VK_SAMPLE_COUNT_64_BIT) {
    return VK_SAMPLE_COUNT_4_BIT; // VK_SAMPLE_COUNT_64_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_32_BIT) {
    return VK_SAMPLE_COUNT_4_BIT; // VK_SAMPLE_COUNT_32_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_16_BIT) {
    return VK_SAMPLE_COUNT_4_BIT; // VK_SAMPLE_COUNT_16_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_8_BIT) {
    return VK_SAMPLE_COUNT_4_BIT; // VK_SAMPLE_COUNT_8_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_4_BIT) {
    return VK_SAMPLE_COUNT_4_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_2_BIT) {
    return VK_SAMPLE_COUNT_2_BIT;
  }

  return VK_SAMPLE_COUNT_1_BIT;
}

float Engine::getMaxSamplerAnisotropy() {
  VkPhysicalDeviceProperties physicalDeviceProperties;
  vkGetPhysicalDeviceProperties(m_context.physicalDevice,
                                &physicalDeviceProperties);
  return physicalDeviceProperties.limits.maxSamplerAnisotropy;
}

} // namespace b3
