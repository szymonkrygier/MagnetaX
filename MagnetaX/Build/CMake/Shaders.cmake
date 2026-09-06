# ==========================
# Vulkan shaders build tool
# ==========================
function(MXBAddShaders out_headers_target out_include_dir)
  find_program(MXB_TOOL_GLSLANG glslangValidator HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin)
  if (NOT MXB_TOOL_GLSLANG)
    message(FATAL_ERROR "glslangValidator not found")
  endif()

  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  set(MXB_DIR_SHADERS "${CMAKE_SOURCE_DIR}/MagnetaX/Shaders")
  set(MXB_DIR_SHADERS_BINARY "${CMAKE_BINARY_DIR}/Shaders/Binary")
  set(MXB_DIR_SHADERS_INCLUDE "${CMAKE_BINARY_DIR}/Shaders/Include")
  set(MXB_DIR_SHADERS_GENERATED "${MXB_DIR_SHADERS_INCLUDE}/MX/Generated/Shaders")

  file(MAKE_DIRECTORY "${MXB_DIR_SHADERS_BINARY}")
  file(MAKE_DIRECTORY "${MXB_DIR_SHADERS_GENERATED}")

  file(GLOB_RECURSE MXB_SOURCE_SHADERS CONFIGURE_DEPENDS
    "${MXB_DIR_SHADERS}/*.vert"
    "${MXB_DIR_SHADERS}/*.frag"
    "${MXB_DIR_SHADERS}/*.comp"
  )

  set(MXB_SOURCE_SHADERS_SPV "")

  foreach(MXB_SHADER_FILE ${MXB_SOURCE_SHADERS})
    file(RELATIVE_PATH MXB_SHADER_RELATIVE "${MXB_DIR_SHADERS}" "${MXB_SHADER_FILE}")

    get_filename_component(MXB_SHADER_DIR "${MXB_SHADER_RELATIVE}" DIRECTORY)
    get_filename_component(MXB_SHADER_NAME "${MXB_SHADER_FILE}" NAME)

    set(MXB_SHADER_OUT "${MXB_DIR_SHADERS_BINARY}/${MXB_SHADER_DIR}/${MXB_SHADER_NAME}.spv")

    get_filename_component(MXB_SHADER_OUT_DIR "${MXB_SHADER_OUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${MXB_SHADER_OUT_DIR}")

    add_custom_command(
      OUTPUT "${MXB_SHADER_OUT}"
      COMMAND "${MXB_TOOL_GLSLANG}" -V "${MXB_SHADER_FILE}" -o "${MXB_SHADER_OUT}"
      DEPENDS "${MXB_SHADER_FILE}"
      VERBATIM
    )

    list(APPEND MXB_SOURCE_SHADERS_SPV "${MXB_SHADER_OUT}")
  endforeach()

  add_custom_target(MXShadersSpv ALL DEPENDS ${MXB_SOURCE_SHADERS_SPV})

  set(MXB_SOURCE_SHADERS_HEADERS "")

  foreach(MXB_SHADER_FILE ${MXB_SOURCE_SHADERS})
    file(RELATIVE_PATH MXB_SHADER_RELATIVE "${MXB_DIR_SHADERS}" "${MXB_SHADER_FILE}")

    get_filename_component(MXB_SHADER_DIR "${MXB_SHADER_RELATIVE}" DIRECTORY)
    get_filename_component(MXB_SHADER_NAME "${MXB_SHADER_FILE}" NAME)

    string(REPLACE "." "_" MXB_SHADER_SYMBOL_NAME "${MXB_SHADER_NAME}")

    get_filename_component(MXB_SHADER_STEM "${MXB_SHADER_FILE}" NAME_WE)
    get_filename_component(MXB_SHADER_EXT "${MXB_SHADER_FILE}" EXT)

    string(SUBSTRING "${MXB_SHADER_EXT}" 1 1 MXB_SHADER_EXT_FIRST)
    string(TOUPPER "${MXB_SHADER_EXT_FIRST}" MXB_SHADER_EXT_FIRST_UPPER)
    string(SUBSTRING "${MXB_SHADER_EXT}" 2 -1 MXB_SHADER_EXT_REST)

    set(MXB_SHADER_EXT_CAP "${MXB_SHADER_EXT_FIRST_UPPER}${MXB_SHADER_EXT_REST}")
    set(MXB_SHADER_HEADER_NAME "VulkanShader${MXB_SHADER_STEM}${MXB_SHADER_EXT_CAP}")

    set(MXB_SHADER_SPV "${MXB_DIR_SHADERS_BINARY}/${MXB_SHADER_DIR}/${MXB_SHADER_NAME}.spv")
    set(MXB_SHADER_HEADER "${MXB_DIR_SHADERS_GENERATED}/${MXB_SHADER_DIR}/${MXB_SHADER_HEADER_NAME}.h")

    string(TOUPPER "${MXB_SHADER_SYMBOL_NAME}" MXB_SHADER_SYMBOL_UPPER)
    set(MXB_SHADER_SYMBOL "MX_GRAPHICS_VULKAN_SHADER_${MXB_SHADER_SYMBOL_UPPER}")

    add_custom_command(
      OUTPUT "${MXB_SHADER_HEADER}"
      COMMAND "${Python3_EXECUTABLE}"
              "${MXB_DIR_BUILD}/Python/SPVToHeader.py"
              "${MXB_SHADER_SPV}"
              "${MXB_SHADER_HEADER}"
              "${MXB_SHADER_SYMBOL}"
      DEPENDS
              "${MXB_SHADER_SPV}"
              "${MXB_DIR_BUILD}/Python/SPVToHeader.py"
      VERBATIM
    )

    list(APPEND MXB_SOURCE_SHADERS_HEADERS "${MXB_SHADER_HEADER}")
  endforeach()

  add_custom_target(MXShadersHeaders ALL DEPENDS ${MXB_SOURCE_SHADERS_HEADERS})
  add_dependencies(MXShadersHeaders MXShadersSpv)

  set(${out_headers_target} MXShadersHeaders PARENT_SCOPE)
  set(${out_include_dir} "${MXB_DIR_SHADERS_INCLUDE}" PARENT_SCOPE)
endfunction()