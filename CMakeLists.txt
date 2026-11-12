cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
project(Borderless-Window-Helper VERSION 2.3.0 LANGUAGES C CXX RC)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX
    "Borderless-Window-Helper-${PROJECT_VERSION}"
    CACHE
    PATH
    "installation directory"
    FORCE)
endif()

add_executable(Borderless-Window-Helper
  WIN32
  src/main.cpp
  src/util.cpp
  third_party/inih/ini.c
)

target_sources(Borderless-Window-Helper PRIVATE "src/Borderless Window Helper.rc")

target_compile_definitions(Borderless-Window-Helper PRIVATE UNICODE _UNICODE)
target_include_directories(Borderless-Window-Helper PRIVATE third_party/inih)

target_link_libraries(Borderless-Window-Helper PRIVATE comctl32 shell32 ole32 uuid user32 gdi32 dwmapi uxtheme)

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
install(FILES "third_party/inih/LICENSE.txt" DESTINATION . RENAME "inih-LICENSE.txt")
