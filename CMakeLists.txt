cmake_minimum_required(VERSION 3.28...3.30)

# The OBS Plugin Template bootstrap reads project metadata from buildspec.json
# and provides the OBS/Qt dependency setup used by GitHub Actions.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 COMPONENTS Widgets Core REQUIRED)

target_link_libraries(${CMAKE_PROJECT_NAME}
    PRIVATE
        OBS::libobs
        OBS::obs-frontend-api
        Qt6::Core
        Qt6::Widgets
)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    src/plugin-main.cpp
    src/diretor-dock.cpp
    src/diretor-dock.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME}
    PROPERTIES
        OUTPUT_NAME ${_name}
)
