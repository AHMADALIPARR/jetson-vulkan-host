cmake_minimum_required(VERSION 3.16)
project(jetson-vulkan-host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Vendored dependencies (AGENTS.md rule 1: vendor means source).
set(VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

# SPIR-V Tools — the gate (validation) and its static library.
set(SPIRV-Headers_SOURCE_DIR ${VENDOR}/SPIRV-Headers CACHE PATH "" FORCE)
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
add_subdirectory(${VENDOR}/SPIRV-Tools ${CMAKE_BINARY_DIR}/spirv-tools)

# glslang — the bootstrap shader compiler (the owner's Pascal-to-SPIR-V
# lowering is the long-term path; glslang compiles the GLSL sources
# under shaders/ meanwhile; both end at a gated SPIR-V module).
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${VENDOR}/glslang ${CMAKE_BINARY_DIR}/glslang)

add_compile_options(-Wall -Wextra)

# ---- device_probe (BUILD-PLAN piece 1) -------------------------------
add_executable(device_probe src/device_probe.cpp)
target_include_directories(device_probe PRIVATE
    ${VENDOR}/Vulkan-Headers/include
    ${VENDOR}/Vulkan-Hpp)
target_link_libraries(device_probe PRIVATE ${CMAKE_DL_LIBS})

# ---- vkhost (BUILD-PLAN pieces 2-5, 7, 8) -----------------------------
add_executable(vkhost src/vkhost.cpp)
target_include_directories(vkhost PRIVATE
    ${VENDOR}/Vulkan-Headers/include
    ${VENDOR}/Vulkan-Hpp
    ${VENDOR}/SPIRV-Tools/include)
target_compile_definitions(vkhost PRIVATE VKHOST_WITH_GATE=1)
target_link_libraries(vkhost PRIVATE SPIRV-Tools-static ${CMAKE_DL_LIBS})

# ---- shaders: compile with the vendored glslang, gate with spirv-val -
set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})
foreach(SHADER ternary_dot.comp fullscreen.vert gradient.frag)
    add_custom_command(
        OUTPUT ${SHADER_OUT}/${SHADER}.spv
        COMMAND glslang-standalone -V
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
                -o ${SHADER_OUT}/${SHADER}.spv
        COMMAND spirv-val ${SHADER_OUT}/${SHADER}.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER} glslang-standalone spirv-val
        COMMENT "glslang + gate: ${SHADER}")
    list(APPEND SPV_FILES ${SHADER_OUT}/${SHADER}.spv)
endforeach()
add_custom_target(shaders ALL DEPENDS ${SPV_FILES})

add_custom_command(TARGET shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E sha256sum ${SPV_FILES} > ${SHADER_OUT}/registry.txt
    COMMAND ${CMAKE_COMMAND} -E cat ${SHADER_OUT}/registry.txt
    COMMENT "module registry (sha256 -> module)")
