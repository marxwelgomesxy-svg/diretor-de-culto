cmake_minimum_required(VERSION 3.20)
project(diretor-de-culto VERSION 2.0.0 LANGUAGES CXX)

set(OBS_SOURCE_DIR "" CACHE PATH "OBS Studio 27.2.4 source directory")
set(OBS_BIN_DIR "" CACHE PATH "OBS Studio 27.2.4 bin/64bit directory")

if(NOT OBS_SOURCE_DIR OR NOT OBS_BIN_DIR)
  message(FATAL_ERROR "OBS_SOURCE_DIR and OBS_BIN_DIR are required")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets)

add_library(diretor-de-culto MODULE
  src/plugin-main.cpp
  src/diretor-dock.cpp
  src/diretor-dock.hpp
)

set(OBS_GENERATED_INCLUDE "${CMAKE_BINARY_DIR}/obs-generated")
file(MAKE_DIRECTORY "${OBS_GENERATED_INCLUDE}")
if(EXISTS "${OBS_SOURCE_DIR}/libobs/obsconfig.h.in")
  configure_file("${OBS_SOURCE_DIR}/libobs/obsconfig.h.in" "${OBS_GENERATED_INCLUDE}/obsconfig.h" @ONLY)
endif()

target_include_directories(diretor-de-culto PRIVATE
  "${OBS_GENERATED_INCLUDE}"
  "${OBS_SOURCE_DIR}/libobs"
  "${OBS_SOURCE_DIR}/UI/obs-frontend-api"
)

target_link_libraries(diretor-de-culto PRIVATE
  "${OBS_BIN_DIR}/obs.lib"
  "${OBS_BIN_DIR}/obs-frontend-api.lib"
  Qt5::Core
  Qt5::Widgets
)

if(MSVC)
  target_compile_definitions(diretor-de-culto PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set_target_properties(diretor-de-culto PROPERTIES
  OUTPUT_NAME "diretor-de-culto"
  PREFIX ""
)

set(DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")
set(INSTALL_PLUGIN_DIR "obs-plugins/64bit")
set(INSTALL_DATA_DIR "data/obs-plugins/diretor-de-culto")

install(TARGETS diretor-de-culto RUNTIME DESTINATION "${INSTALL_PLUGIN_DIR}")
install(DIRECTORY "${DATA_DIR}/" DESTINATION "${INSTALL_DATA_DIR}")
