cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
project(Borderless-Window-Helper VERSION 2.2.0 LANGUAGES CXX)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX
    "Borderless-Window-Helper-${PROJECT_VERSION}"
    CACHE
    PATH
    "installation directory"
    FORCE)
endif()

find_program(CCACHE ccache)
if(CCACHE)
  message(STATUS "found ccache: ${CCACHE}")
  set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
endif()

add_executable(Borderless-Window-Helper
  WIN32
  inifile.cpp
  main.cpp
  util.cpp
)

target_sources(Borderless-Window-Helper PRIVATE "Borderless Window Helper.rc")

target_compile_definitions(Borderless-Window-Helper PRIVATE UNICODE _UNICODE)

include(cmake/CPM.cmake)

option(NANA_CMAKE_ENABLE_CONF "enable config.hpp" ON)

CPMAddPackage(
  NAME nana
  GITHUB_REPOSITORY cnjinhao/nana
  GIT_TAG 3ff1380bcd482d6cfa2c7787923bdd0b82230f6f
)

target_link_libraries(Borderless-Window-Helper PRIVATE nana::nana)

if(NOT MSVC)
  target_link_options(Borderless-Window-Helper
    PRIVATE
    -municode
    -static
    -static-libgcc
    -static-libstdc++
  )
endif()

if(MSVC)
  target_compile_options(Borderless-Window-Helper
    PRIVATE
    /MT # build static exe
  )
endif()

target_compile_features(Borderless-Window-Helper PRIVATE cxx_std_17)

set(CPACK_GENERATOR ZIP)
include(CPack)
install(TARGETS Borderless-Window-Helper RUNTIME DESTINATION .)
