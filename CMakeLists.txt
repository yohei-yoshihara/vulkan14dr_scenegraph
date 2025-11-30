cmake_minimum_required(VERSION 3.31)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(app)

add_executable(${PROJECT_NAME}
  src/main.hpp src/main.cpp
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PUBLIC $<$<CONFIG:Debug>:DEBUG>)
if (MSVC)
    target_compile_options(${PROJECT_NAME} PUBLIC /W4)
else()
    target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()
target_include_directories(${PROJECT_NAME} PRIVATE src)

add_subdirectory(b3EngineLib)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC "b3EngineLib/src")
target_link_libraries(${PROJECT_NAME} PUBLIC b3Engine)

# For Visual Studio, copy the shaders to the folder where the executable file is located.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                   b3EngineLib/shaders
                   $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                   "${CMAKE_SOURCE_DIR}/images"
                   $<TARGET_FILE_DIR:${PROJECT_NAME}>/images)

# b3EngineTests
enable_testing()
add_subdirectory(b3EngineTests)
add_test(NAME b3EngineTests COMMAND $<TARGET_FILE:b3EngineTests>)

# appLib
add_subdirectory(appLib)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC appLib)
target_link_libraries(${PROJECT_NAME} PUBLIC appLib)

# appTests
add_subdirectory(appTests)
add_test(NAME appTests COMMAND $<TARGET_FILE:appTests>)
