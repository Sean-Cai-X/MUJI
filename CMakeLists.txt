cmake_minimum_required(VERSION 3.20)
project(MUJI LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4)
    add_definitions(-D_USE_MATH_DEFINES -DNOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unknown-pragmas)
endif()

# ---------- OpenGL ----------
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# ---------- GLFW: prefer system package, fallback to FetchContent ----------
find_package(glfw3 3.4 QUIET)
if(TARGET glfw)
    message(STATUS "MUJI: using system GLFW")
else()
    message(STATUS "MUJI: system GLFW not found, falling back to FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON   CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
endif()

# ---------- ImGui docking branch (vendored, no .git inside) ----------
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/3rdparty/imgui-docking)
if(NOT EXISTS ${IMGUI_DIR}/imgui.h)
    message(FATAL_ERROR
        "Missing vendored ImGui docking at ${IMGUI_DIR}.\n"
        "Run: git clone --depth 1 --branch docking https://github.com/ocornut/imgui.git 3rdparty/imgui-docking\n"
        "Then: rm -rf 3rdparty/imgui-docking/.git")
endif()

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
add_library(muji_imgui STATIC ${IMGUI_SOURCES})
target_include_directories(muji_imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(muji_imgui PUBLIC glfw OpenGL::GL)
target_compile_definitions(muji_imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# ---------- cppjieba (header-only, vendored) ----------
set(CPPJIEBA_DIR ${CMAKE_SOURCE_DIR}/3rdparty/cppjieba)
if(NOT EXISTS ${CPPJIEBA_DIR}/include/cppjieba/Jieba.hpp)
    message(FATAL_ERROR
        "Missing vendored cppjieba at ${CPPJIEBA_DIR}.\n"
        "Run: git clone --depth 1 https://github.com/yanyiwu/cppjieba.git 3rdparty/cppjieba\n"
        "Then: rm -rf 3rdparty/cppjieba/.git")
endif()
add_library(muji_cppjieba INTERFACE)
target_include_directories(muji_cppjieba INTERFACE ${CPPJIEBA_DIR}/include)

# ---------- WordNet C API (system library, optional — skip gracefully) ----------
find_library(WORDNET_LIB wordnet)
find_path(WORDNET_INC wn.h)
if(WORDNET_LIB AND WORDNET_INC)
    add_library(muji_wordnet INTERFACE)
    target_include_directories(muji_wordnet INTERFACE ${WORDNET_INC})
    target_link_libraries(muji_wordnet INTERFACE ${WORDNET_LIB})
    message(STATUS "MUJI: WordNet C API found (${WORDNET_LIB}, ${WORDNET_INC})")
    set(MUJI_HAS_WORDNET ON)
else()
    message(STATUS "MUJI: WordNet C API NOT found — WordNet adapter disabled")
    set(MUJI_HAS_WORDNET OFF)
endif()

# ---------- GoldenDict adapter (pure socket, no lib dep) ----------
# dictd is a TCP daemon; we talk RFC 2229 directly.
# We detect dictd/goldendict apt packages via pkg-config isn't needed.
# We only set a compile def for the adapter if we want to auto-start dictd.
find_program(DICTD_BIN dictd)
if(DICTD_BIN)
    message(STATUS "MUJI: dictd binary found — GoldenDict adapter enabled")
    set(MUJI_HAS_DICTD ON)
else()
    message(STATUS "MUJI: dictd NOT found — GoldenDict adapter compiles but dictd not auto-started")
    set(MUJI_HAS_DICTD OFF)
endif()

# ---------- StarDict adapter (pure C++, scans local dic/extracted/) ----------
find_package(zlib QUIET)   # not needed — we pre-decompress with dictunzip
file(GLOB STARDICT_IFO CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/dic/extracted/*/*.ifo
)
if(STARDICT_IFO)
    message(STATUS "MUJI: StarDict dicts found (${STARDICT_IFO})")
endif()
set(MUJI_HAS_STARDICT ON)   # always on — pure C++ no external deps

# ---------- Kiwix (ZIM reader) — system libkiwix-dev + libzim-dev ----------
find_library(ZIM_LIB zim)
find_path(ZIM_INC zim/archive.h)
if(ZIM_LIB AND ZIM_INC)
    add_library(muji_zim INTERFACE)
    target_include_directories(muji_zim INTERFACE ${ZIM_INC})
    target_link_libraries(muji_zim INTERFACE ${ZIM_LIB})
    message(STATUS "MUJI: libzim found (${ZIM_LIB}) — Kiwix adapter enabled")
    set(MUJI_HAS_KIWIX ON)
else()
    message(STATUS "MUJI: libzim NOT found — Kiwix adapter disabled")
    set(MUJI_HAS_KIWIX OFF)
endif()

# ---------- codex-lan-agent MCP bridge adapter (pure socket + vendored nlohmann/json) ----------
# Zero external deps — nlohmann/json is a vendored directory at 3rdparty/nlohmann/
# We set include dir to 3rdparty/ (parent) so #include <nlohmann/json.hpp> resolves.
# We only compile this adapter; lan-agent itself runs as a separate process.
set(NLOHMANN_PARENT ${CMAKE_SOURCE_DIR}/3rdparty)
if(EXISTS ${NLOHMANN_PARENT}/nlohmann/json.hpp)
    message(STATUS "MUJI: nlohmann/json found at ${NLOHMANN_PARENT}/nlohmann — LanAgentAdapter enabled")
    set(MUJI_HAS_LANAGENT ON)
    add_library(muji_nlohmann INTERFACE)
    target_include_directories(muji_nlohmann INTERFACE ${NLOHMANN_PARENT})
else()
    message(STATUS "MUJI: nlohmann/json NOT found — LanAgentAdapter disabled")
    set(MUJI_HAS_LANAGENT OFF)
endif()

# ---------- MUJI executable ----------
# GLOB everything under src/ EXCEPT native/ (those are standalone CLIs)
# Conditional adapter .cpp files added below
file(GLOB MUJI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/src/adapters/JiebaAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/adapters/JiebaAdapter.h
    ${CMAKE_SOURCE_DIR}/src/adapters/GoldenDictAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/adapters/GoldenDictAdapter.h
    ${CMAKE_SOURCE_DIR}/src/adapters/StarDictAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/adapters/StarDictAdapter.h
    ${CMAKE_SOURCE_DIR}/src/adapters/WordNetAdapter.h
    ${CMAKE_SOURCE_DIR}/src/data/*.h
    ${CMAKE_SOURCE_DIR}/src/glue/*.h
)

if(MUJI_HAS_WORDNET)
    list(APPEND MUJI_SOURCES
        ${CMAKE_SOURCE_DIR}/src/adapters/WordNetAdapter.cpp
    )
endif()

if(MUJI_HAS_KIWIX)
    list(APPEND MUJI_SOURCES
        ${CMAKE_SOURCE_DIR}/src/adapters/KiwixAdapter.cpp
        ${CMAKE_SOURCE_DIR}/src/adapters/KiwixAdapter.h
    )
endif()

# Layer 4: TraceableVector stub (always on — pure C++, no deps)
list(APPEND MUJI_SOURCES
    ${CMAKE_SOURCE_DIR}/src/adapters/VectorAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/adapters/VectorAdapter.h
)

# Bridge: codex-lan-agent MCP adapter (HTTP JSON-RPC over plain socket)
if(MUJI_HAS_LANAGENT)
    list(APPEND MUJI_SOURCES
        ${CMAKE_SOURCE_DIR}/src/adapters/LanAgentAdapter.cpp
        ${CMAKE_SOURCE_DIR}/src/adapters/LanAgentAdapter.h
    )
endif()

# ── 3D Scene Layer (Interface-only for now — no external 3D engine) ──
# 纯虚接口 + CPU 3D→2D 投影渲染 + 语义场景构建器
list(APPEND MUJI_SOURCES
    ${CMAKE_SOURCE_DIR}/src/3d/SoftwareRenderer3D.cpp
    ${CMAKE_SOURCE_DIR}/src/3d/SoftwareRenderer3D.h
    ${CMAKE_SOURCE_DIR}/src/3d/SemanticSceneBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/3d/SemanticSceneBuilder.h
    ${CMAKE_SOURCE_DIR}/src/3d/ISceneGraph3D.h
    ${CMAKE_SOURCE_DIR}/src/3d/ICamera3D.h
    ${CMAKE_SOURCE_DIR}/src/3d/ISceneRenderer.h
    ${CMAKE_SOURCE_DIR}/src/3d/SceneTypes.h
)

add_executable(muji ${MUJI_SOURCES})
target_include_directories(muji PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(muji PRIVATE muji_imgui muji_cppjieba glfw OpenGL::GL ${CMAKE_DL_LIBS})
target_compile_definitions(muji PRIVATE
    $<$<BOOL:${MUJI_HAS_WORDNET}>:MUJI_HAS_WORDNET>
    $<$<BOOL:${MUJI_HAS_LANAGENT}>:MUJI_HAS_LANAGENT>
)
if(MUJI_HAS_WORDNET)
    target_link_libraries(muji PRIVATE muji_wordnet)
endif()

if(MUJI_HAS_LANAGENT)
    target_link_libraries(muji PRIVATE muji_nlohmann)
endif()

if(MUJI_HAS_KIWIX)
    target_link_libraries(muji PRIVATE muji_zim)
    target_compile_definitions(muji PRIVATE MUJI_HAS_KIWIX)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(muji PRIVATE pthread)
endif()

# ---------- Standalone native entry points (each compiles independently) ----------
# These exist to prove the "native modules keep their own entry points" rule.
# They bypass the glue layer entirely.

add_executable(jieba_cli ${CMAKE_SOURCE_DIR}/src/native/jieba_cli.cpp)
target_link_libraries(jieba_cli PRIVATE muji_cppjieba)
set_target_properties(jieba_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MUJI_HAS_WORDNET)
    add_executable(wn_cli ${CMAKE_SOURCE_DIR}/src/native/wn_cli.cpp)
    target_link_libraries(wn_cli PRIVATE muji_wordnet)
    set_target_properties(wn_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

add_executable(dict_cli ${CMAKE_SOURCE_DIR}/src/native/dict_cli.cpp)
set_target_properties(dict_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(stardict_cli ${CMAKE_SOURCE_DIR}/src/native/stardict_cli.cpp)
set_target_properties(stardict_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MUJI_HAS_KIWIX)
    add_executable(kiwix_cli ${CMAKE_SOURCE_DIR}/src/native/kiwix_cli.cpp)
    target_link_libraries(kiwix_cli PRIVATE muji_zim)
    set_target_properties(kiwix_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()
