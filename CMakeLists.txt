cmake_minimum_required(VERSION 3.16)

# This top-level project is an ESP-IDF friendly wrapper around Retro-Go's
# multi-application build. The real firmware image is produced by rg_tool.py.
if(NOT EXISTS "$ENV{IDF_PATH}/tools/cmake/toolchain-esp32s31.cmake"
   AND EXISTS "$ENV{HOME}/.espressif/master/esp-idf/tools/cmake/toolchain-esp32s31.cmake")
    set(ENV{IDF_PATH} "$ENV{HOME}/.espressif/master/esp-idf")
endif()

if(NOT DEFINED ENV{IDF_TOOLS_PATH})
    set(ENV{IDF_TOOLS_PATH} "$ENV{HOME}/.espressif/tools")
endif()
set(ENV{IDF_COMPONENT_MANAGER} "0")
set(ENV{ESP_ROM_ELF_DIR} "$ENV{IDF_TOOLS_PATH}/esp-rom-elfs")
set(ENV{PYTHON} "$ENV{IDF_TOOLS_PATH}/python/master/venv/bin/python")
set(ENV{ESPTOOL_WRAPPER} "${CMAKE_CURRENT_LIST_DIR}/tools/esptool_fast_wrapper.py")
set(PYTHON "$ENV{PYTHON}" CACHE FILEPATH "Python interpreter for ESP-IDF" FORCE)

if(NOT "$ENV{PATH}" MATCHES "riscv32-esp-elf")
    file(GLOB RG_RISCV_TOOLCHAIN_BINS
        LIST_DIRECTORIES true
        "$ENV{IDF_TOOLS_PATH}/riscv32-esp-elf/*/riscv32-esp-elf/bin"
    )
    list(SORT RG_RISCV_TOOLCHAIN_BINS)
    list(POP_BACK RG_RISCV_TOOLCHAIN_BINS RG_RISCV_TOOLCHAIN_BIN)
    if(RG_RISCV_TOOLCHAIN_BIN)
        set(ENV{PATH} "${RG_RISCV_TOOLCHAIN_BIN}:$ENV{PATH}")
    endif()
endif()

set(IDF_TARGET "esp32s31" CACHE STRING "ESP-IDF target")
set(COMPONENTS "main")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(retro-go-s31)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(RG_TARGET "esp32-s31-korvo-1" CACHE STRING "Retro-Go hardware target")
set(RG_APPS "launcher;retro-core;gbsp" CACHE STRING "Retro-Go apps to include in the image")
set(RG_TOOL "${CMAKE_SOURCE_DIR}/rg_tool.py")
set(RG_STAGED_IMAGE "${CMAKE_BINARY_DIR}/retro-go-s31-full.img")
file(RELATIVE_PATH RG_STAGED_IMAGE_FLASH_PATH "${CMAKE_BINARY_DIR}" "${RG_STAGED_IMAGE}")
file(GLOB_RECURSE RG_IMAGE_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/components/retro-go/*"
    "${CMAKE_SOURCE_DIR}/components/espressif__esp_codec_dev/*"
    "${CMAKE_SOURCE_DIR}/launcher/main/*"
    "${CMAKE_SOURCE_DIR}/launcher/CMakeLists.txt"
    "${CMAKE_SOURCE_DIR}/retro-core/main/*"
    "${CMAKE_SOURCE_DIR}/retro-core/components/*"
    "${CMAKE_SOURCE_DIR}/retro-core/CMakeLists.txt"
    "${CMAKE_SOURCE_DIR}/gbsp/main/*"
    "${CMAKE_SOURCE_DIR}/gbsp/components/*"
    "${CMAKE_SOURCE_DIR}/gbsp/CMakeLists.txt"
    "${CMAKE_SOURCE_DIR}/tools/*"
    "${CMAKE_SOURCE_DIR}/rg_tool.py"
)
list(FILTER RG_IMAGE_INPUTS EXCLUDE REGEX "/\\.cache/")
set(RG_ENV
    ${CMAKE_COMMAND} -E env
    "IDF_PATH=$ENV{IDF_PATH}"
    "IDF_TOOLS_PATH=$ENV{IDF_TOOLS_PATH}"
    "IDF_PYTHON_ENV_PATH=$ENV{IDF_TOOLS_PATH}/python/master/venv"
    "PYTHON=$ENV{IDF_TOOLS_PATH}/python/master/venv/bin/python"
    "ESP_ROM_ELF_DIR=$ENV{IDF_TOOLS_PATH}/esp-rom-elfs"
    "IDF_COMPONENT_MANAGER=0"
    "ESPTOOL_WRAPPER=${CMAKE_SOURCE_DIR}/tools/esptool_fast_wrapper.py"
    "ESPBAUD=1500000"
    "PATH=$ENV{PATH}:$ENV{IDF_PATH}/tools:$ENV{IDF_TOOLS_PATH}/python/master/venv/bin"
)

add_custom_command(
    OUTPUT ${RG_STAGED_IMAGE}
    COMMAND ${RG_ENV} ${Python3_EXECUTABLE} ${RG_TOOL} --target ${RG_TARGET} build-img ${RG_APPS}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/stage_rg_image.py
        --source-dir ${CMAKE_SOURCE_DIR}
        --target ${RG_TARGET}
        --output ${RG_STAGED_IMAGE}
    DEPENDS ${RG_IMAGE_INPUTS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
    VERBATIM
    COMMENT "Build Retro-Go S31 multi-app flash image"
)

add_custom_target(rg-image ALL DEPENDS ${RG_STAGED_IMAGE})

if(TARGET flash)
    add_dependencies(flash rg-image)
    set_property(TARGET flash PROPERTY IMAGES "0x0 ${RG_STAGED_IMAGE_FLASH_PATH}")
    set_property(TARGET flash PROPERTY FLASH_FILE "\"0x0\" : \"${RG_STAGED_IMAGE_FLASH_PATH}\"")
    set_property(TARGET flash PROPERTY FLASH_ENTRY "\"retro-go-s31-full\" : { \"offset\" : \"0x0\", \"file\" : \"${RG_STAGED_IMAGE_FLASH_PATH}\", \"encrypted\" : \"false\" }")
endif()

if(TARGET app-flash)
    add_dependencies(app-flash rg-image)
    set_property(TARGET app-flash PROPERTY IMAGES "0x0 ${RG_STAGED_IMAGE_FLASH_PATH}")
    set_property(TARGET app-flash PROPERTY FLASH_FILE "\"0x0\" : \"${RG_STAGED_IMAGE_FLASH_PATH}\"")
    set_property(TARGET app-flash PROPERTY FLASH_ENTRY "\"retro-go-s31-full\" : { \"offset\" : \"0x0\", \"file\" : \"${RG_STAGED_IMAGE_FLASH_PATH}\", \"encrypted\" : \"false\" }")
endif()

add_custom_target(rg-flash
    COMMAND ${RG_ENV} ${Python3_EXECUTABLE} ${RG_TOOL} --target ${RG_TARGET} install ${RG_APPS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
    COMMENT "Build and flash Retro-Go S31 image"
)

add_custom_target(rg-build-apps
    COMMAND ${RG_ENV} ${Python3_EXECUTABLE} ${RG_TOOL} --target ${RG_TARGET} build ${RG_APPS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
    COMMENT "Build Retro-Go S31 apps without packing"
)

add_custom_target(rg-clean
    COMMAND ${RG_ENV} ${Python3_EXECUTABLE} ${RG_TOOL} --target ${RG_TARGET} clean ${RG_APPS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
    COMMENT "Clean Retro-Go S31 app builds"
)

add_custom_target(rg-monitor
    COMMAND ${RG_ENV} ${Python3_EXECUTABLE} ${RG_TOOL} --target ${RG_TARGET} monitor launcher
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
    COMMENT "Open serial monitor"
)
