cmake_minimum_required(VERSION 3.31)

# If you want to place the dependent libraries other than under the `build` folder, comment out the following:
# if(WIN32)
#     file(TO_CMAKE_PATH "$ENV{USERPROFILE}" USER_HOME_DIRECTORY)
# else()
#     set(USER_HOME_DIRECTORY $ENV{HOME})
# endif()
# set(FETCHCONTENT_BASE_DIR "${USER_HOME_DIRECTORY}/.cache/FetchContent")
include(FetchContent)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(b3Engine)

add_library(${PROJECT_NAME}
  src/b3/b3.hpp src/b3/b3.cpp
  src/b3/engine.hpp src/b3/engine.cpp
  src/b3/common.hpp src/b3/common.cpp
  src/b3/types.hpp src/b3/types.cpp
  src/b3/mesh.hpp src/b3/mesh.cpp
  src/b3/texture.hpp src/b3/texture.cpp
  src/b3/node.hpp src/b3/node.cpp
  src/b3/camera.hpp src/b3/camera.cpp
  src/b3/frustum_culling.hpp src/b3/frustum_culling.cpp

  src/b3/primitives/CubeMesh.hpp src/b3/primitives/CubeMesh.cpp
  src/b3/primitives/PlaneMesh.hpp src/b3/primitives/PlaneMesh.cpp
  src/b3/primitives/SphereMesh.hpp src/b3/primitives/SphereMesh.cpp

  shaders/scene.vert.spv
  shaders/scene.frag.spv
  shaders/shadow.vert.spv
  shaders/shadow.frag.spv
)

source_group(shaders FILES
  shaders/scene.vert
  shaders/scene.frag
  shaders/shadow.vert
  shaders/shadow.frag
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PUBLIC $<$<CONFIG:Debug>:DEBUG>)
if (MSVC)
    target_compile_options(${PROJECT_NAME} PUBLIC /W4)
else()
    target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-nullability-extension)
endif()
target_include_directories(${PROJECT_NAME} PRIVATE src)

# Vulkan
find_package(Vulkan REQUIRED COMPONENTS glslc)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${Vulkan_INCLUDE_DIR})
# target_link_libraries(${PROJECT_NAME} PRIVATE ${Vulkan_LIBRARIES})

# SDL
set(SDL_TESTS OFF)
set(SDL_TEST_LIBRARY OFF)
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
FetchContent_Declare(
  sdl
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.26)
FetchContent_MakeAvailable(sdl)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${sdl_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC SDL3::SDL3)

# spdlog
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.16.0)
FetchContent_MakeAvailable(spdlog)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${spdlog_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC spdlog::spdlog)

# glm
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.2)
FetchContent_MakeAvailable(glm)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${glm_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC glm::glm)

# vk-bootstrap
FetchContent_Declare(
  vk_bootstrap
  GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
  GIT_TAG v1.4.328 
)
FetchContent_MakeAvailable(vk_bootstrap)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${vk_bootstrap_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PUBLIC vk-bootstrap::vk-bootstrap)

# Vulkan Memory Allocator
FetchContent_Declare(
  vma
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG v3.3.0 
)
FetchContent_MakeAvailable(vma)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${vma_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME} PUBLIC VulkanMemoryAllocator)

# volk
FetchContent_Declare(
  volk
  GIT_REPOSITORY https://github.com/zeux/volk.git
  GIT_TAG vulkan-sdk-1.4.328.1 
)
FetchContent_MakeAvailable(volk)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${volk_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC volk::volk)

# stb
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG f1c79c02822848a9bed4315b12c8c8f3761e1296
)
FetchContent_MakeAvailable(stb)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${stb_SOURCE_DIR})

# json
FetchContent_Declare(
  json 
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(json)
target_link_libraries(${PROJECT_NAME} PUBLIC nlohmann_json::nlohmann_json)

# fastgltf
FetchContent_Declare(
  fastgltf
  GIT_REPOSITORY https://github.com/spnda/fastgltf.git
  GIT_TAG v0.9.0
)
FetchContent_MakeAvailable(fastgltf)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${fastgltf_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC fastgltf)

# imgui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.4
)
FetchContent_MakeAvailable(imgui)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${imgui_SOURCE_DIR})

# shaders

function(compile_shaders name)
  set(GLSLC ${Vulkan_GLSLC_EXECUTABLE})
  set(GLSLC_OPTIONS -g --target-env=vulkan1.3)

  add_custom_command(
      OUTPUT shaders/${name}.vert.spv
      COMMAND ${CMAKE_COMMAND} -E make_directory shaders
      COMMAND ${GLSLC} ${GLSLC_OPTIONS} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.vert -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.vert.spv 
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.vert
      COMMENT "Compiling vertex shader"
      )

  add_custom_command(
      OUTPUT shaders/${name}.frag.spv
      COMMAND ${CMAKE_COMMAND} -E make_directory shaders
      COMMAND ${GLSLC} ${GLSLC_OPTIONS} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.frag -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.frag.spv 
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.frag
      COMMENT "Compiling fragment shader"
      )

  add_custom_target(compile_${name}_shaders DEPENDS shaders/${name}.vert.spv shaders/${name}.frag.spv)
  add_dependencies(${CMAKE_PROJECT_NAME} compile_${name}_shaders)
endfunction()

compile_shaders("scene")
compile_shaders("shadow")

# For Visual Studio, copy the shaders to the folder where the executable file is located.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                   ${CMAKE_CURRENT_BINARY_DIR}/shaders
                   $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders)
