cmake_minimum_required(VERSION 3.31)
project(appLib VERSION 1.0 LANGUAGES CXX)
add_library(${PROJECT_NAME}
  app.hpp app.cpp
)
