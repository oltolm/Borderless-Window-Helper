cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
project(Borderless-Window-Helper VERSION 2.3.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
  set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
endif()

add_executable(Borderless-Window-Helper
  WIN32
  src/inifile.cpp
  src/main.cpp
  src/util.cpp
)

target_sources(Borderless-Window-Helper PRIVATE "src/Borderless Window Helper.rc")

target_compile_definitions(Borderless-Window-Helper PRIVATE UNICODE _UNICODE)

include(cmake/CPM.cmake)

option(NANA_CMAKE_ENABLE_CONF "enable config.hpp" ON)

CPMAddPackage(
  NAME nana
  GITHUB_REPOSITORY cnjinhao/nana
  GIT_TAG efa10557660b6454fc70cbb7f5a64796b1feb882
)

target_compile_definitions(nana PRIVATE NANA_ENABLE_VIRTUAL_KEYBOARD)

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

set_target_properties(Borderless-Window-Helper PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_features(Borderless-Window-Helper PRIVATE cxx_std_17)

set(CPACK_GENERATOR ZIP)
include(CPack)
install(TARGETS Borderless-Window-Helper RUNTIME DESTINATION .)
