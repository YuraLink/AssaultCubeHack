cmake_minimum_required(VERSION 3.5)

project(AssaultCubeHack LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FindFreetype)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm REQUIRED)
include_directories(${GLEW_INCLUDE_DIRS})
include_directories(${FREETYPE_INCLUDE_DIRS})
include_directories( ${OPENGL_INCLUDE_DIRS}  ${GLUT_INCLUDE_DIRS} ${GLM_INCLUDE_DIRS})
add_library(AssaultCubeHack SHARED main.cpp sdk.h)

target_link_libraries(AssaultCubeHack m)
target_link_libraries(AssaultCubeHack OpenGL::GL)
target_link_libraries(AssaultCubeHack freetype)
target_link_libraries(AssaultCubeHack ${GLEW_LIBRARIES} glm::glm)


