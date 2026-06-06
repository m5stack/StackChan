file(GLOB_RECURSE STACK_CHAN_SOURCES
    "apps/*.c"
    "apps/*.cc"
    "apps/*.cpp"
    "assets/*.c"
    "assets/*.cc"
    "assets/*.cpp"
    "hal/*.c"
    "hal/*.cc"
    "hal/*.cpp"
    "mcp/*.c"
    "mcp/*.cc"
    "mcp/*.cpp"
    "stackchan/*.c"
    "stackchan/*.cc"
    "stackchan/*.cpp"
)
list(FILTER STACK_CHAN_SOURCES EXCLUDE REGEX "(^|.*/)apps/app_template/.*")
set(STACK_CHAN_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio"
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/common"
)
list(APPEND STACK_CHAN_SOURCES main.cpp)

set(SOURCES
    "audio/audio_codec.cc"
    "audio/audio_system.cc"
    "audio/audio_bus.cc"
    "audio/audio_capture_worker.cc"
    "audio/audio_playback_worker.cc"
    "audio/audio_power_controller.cc"
    "audio/audio_processor_factory.cc"
    "audio/audio_test_controller.cc"
    "audio/esp_wake_word.cc"
    "audio/no_audio_processor.cc"
    "audio/opus_codec_worker.cc"
    "audio/task_worker.cc"
    "audio/voice_processor_controller.cc"
    "audio/wake_word_controller.cc"
    "audio/wake_word_factory.cc"
    "audio/demuxer/ogg_demuxer.cc"
    "audio/processors/afe_audio_processor.cc"
    "audio/wake_words/afe_custom_wake_word.cc"
    "audio/wake_words/afe_wake_word.cc"
    "audio/wake_words/custom_wake_word.cc"
    "audio/wake_words/wake_word_history_encoder.cc"
    "display/display.cc"
    "display/lvgl_display/lvgl_display.cc"
    "display/lvgl_display/emoji_collection.cc"
    "display/lvgl_display/lvgl_theme.cc"
    "display/lvgl_display/lvgl_font.cc"
    "display/lvgl_display/lvgl_image.cc"
    "display/lvgl_display/jpg/image_to_jpeg.cpp"
    "display/lvgl_display/jpg/jpeg_to_image.c"
)

set(INCLUDE_DIRS
    "."
    "display"
    "display/lvgl_display"
    "display/lvgl_display/jpg"
    "audio"
    "audio/demuxer"
    "boards/common"
)

# Add board common files
list(APPEND SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/application_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/activation_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/assets_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/bootstrap_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/control_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/device_state_machine_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/event_loop_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/main_loop_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/mcp_server_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/network_event_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ota_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/protocol_message_handler_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/settings_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/state_event_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/system_info_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/protocol_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/voice_session_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/websocket_protocol_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/common/backlight.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/common/board.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/common/i2c_device.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/common/power_save_timer.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/common/press_to_talk_mcp_tool.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/common/system_reset.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/common/wifi_board.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/common/axp2101.cc"
    ${STACK_CHAN_SOURCES}
)
list(PREPEND INCLUDE_DIRS ${STACK_CHAN_INCLUDE_DIRS})


idf_build_get_property(build_components BUILD_COMPONENTS)

set(STACKCHAN_ASSET_ARCHIVE "${PROJECT_DIR}/main/assets/assets.tar.gz")
set(STACKCHAN_EXTRACTED_ASSET_DIR "${CMAKE_BINARY_DIR}/stackchan_assets")
set(STACKCHAN_RUNTIME_ASSETS_DIR "${STACKCHAN_EXTRACTED_ASSET_DIR}/main_assets")
set(STACKCHAN_FONTS_PATH "${STACKCHAN_EXTRACTED_ASSET_DIR}/stackchan_fonts")

if(NOT EXISTS "${STACKCHAN_ASSET_ARCHIVE}")
    message(FATAL_ERROR "Missing StackChan asset archive: ${STACKCHAN_ASSET_ARCHIVE}")
endif()

file(MAKE_DIRECTORY "${STACKCHAN_EXTRACTED_ASSET_DIR}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xzf "${STACKCHAN_ASSET_ARCHIVE}"
    WORKING_DIRECTORY "${STACKCHAN_EXTRACTED_ASSET_DIR}"
    RESULT_VARIABLE STACKCHAN_ASSET_EXTRACT_RESULT
)
if(NOT STACKCHAN_ASSET_EXTRACT_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to extract StackChan asset archive: ${STACKCHAN_ASSET_ARCHIVE}")
endif()

list(APPEND SOURCES "${STACKCHAN_RUNTIME_ASSETS_DIR}/fonts/MontserratSemiBold26.c")

# Function to find component dynamically by pattern
function(find_component_by_pattern PATTERN COMPONENT_VAR PATH_VAR)
    foreach(COMPONENT ${build_components})
        if(COMPONENT MATCHES "${PATTERN}")
            set(${COMPONENT_VAR} ${COMPONENT} PARENT_SCOPE)
            idf_component_get_property(COMPONENT_PATH ${COMPONENT} COMPONENT_DIR)
            set(${PATH_VAR} "${COMPONENT_PATH}" PARENT_SCOPE)
            break()
        endif()
    endforeach()
endfunction()

# Set default BUILTIN_TEXT_FONT and BUILTIN_ICON_FONT
set(BUILTIN_TEXT_FONT font_puhui_14_1)
set(BUILTIN_ICON_FONT font_awesome_14_1)

# Add board files according to BOARD_TYPE
# Set default assets if the board uses partition table V2
if(CONFIG_BOARD_TYPE_M5STACK_STACK_CHAN)
    set(BOARD_TYPE "m5stack-stack-chan")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
endif()

# Select audio processor according to Kconfig
if(CONFIG_USE_AUDIO_PROCESSOR)
else()
endif()
if(CONFIG_IDF_TARGET_ESP32S3 OR CONFIG_IDF_TARGET_ESP32P4)
else()
endif()

# Auto Select Additional Sources
if (CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING)
    list(APPEND SOURCES "boards/common/blufi.cpp")
endif ()
# Select language directory according to Kconfig.
if(CONFIG_LANGUAGE)
    set(LANG_DIR "${CONFIG_LANGUAGE}")
else()
    set(LANG_DIR "en-US")
endif()

set(LANG_JSON "${STACKCHAN_RUNTIME_ASSETS_DIR}/locales/${LANG_DIR}/language.json")
if(NOT EXISTS "${LANG_JSON}")
    message(FATAL_ERROR "Configured language '${LANG_DIR}' has no language.json under ${STACKCHAN_RUNTIME_ASSETS_DIR}/locales")
endif()

set(LANG_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/assets/lang_config.h")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/assets")

set(LANG_SOUND_FILES
    err_pin.ogg
    err_reg.ogg
    upgrade.ogg
    wificonfig.ogg
)

set(LANG_SOUNDS "")
set(LANG_SOUND_ARGS "")
foreach(FILENAME ${LANG_SOUND_FILES})
    list(APPEND LANG_SOUND_ARGS "--sound" "${FILENAME}")
    set(SOUND_FILE "${STACKCHAN_RUNTIME_ASSETS_DIR}/locales/${LANG_DIR}/${FILENAME}")
    if(EXISTS "${SOUND_FILE}")
        list(APPEND LANG_SOUNDS "${SOUND_FILE}")
    else()
        set(EN_SOUND "${STACKCHAN_RUNTIME_ASSETS_DIR}/locales/en-US/${FILENAME}")
        if(NOT LANG_DIR STREQUAL "en-US" AND EXISTS "${EN_SOUND}")
            list(APPEND LANG_SOUNDS "${EN_SOUND}")
            message(STATUS "Using en-US fallback for missing audio: ${FILENAME}")
        else()
            message(FATAL_ERROR "Configured language '${LANG_DIR}' is missing required audio: ${FILENAME}")
        endif()
    endif()
endforeach()

file(GLOB COMMON_SOUNDS 
    ${STACKCHAN_RUNTIME_ASSETS_DIR}/common/*.ogg
    ${STACKCHAN_RUNTIME_ASSETS_DIR}/sfx/*.ogg
)
list(APPEND INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/generated")

idf_component_register(SRCS ${SOURCES}
                    EMBED_FILES ${LANG_SOUNDS} ${COMMON_SOUNDS}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    WHOLE_ARCHIVE
                    PRIV_REQUIRES
                        esp_pm
                        esp_psram
                        esp_netif
                        esp_driver_gpio
                        esp_driver_uart
                        esp_driver_spi
                        esp_driver_i2c
                        esp_driver_i2s
                        esp_driver_jpeg
                        esp_driver_ppa
                        esp_app_format
                        app_update
                        spi_flash
                        console
                        efuse
                        bt
                        fatfs
                        ArduinoJson
                        esp-now
                        mooncake
                        mooncake_log
                        stackchan_fonts
                        smooth_ui_toolkit
                    )

# Use target_compile_definitions to define BOARD_TYPE, BOARD_NAME
# If BOARD_NAME is empty, use BOARD_TYPE
if(NOT BOARD_NAME)
    set(BOARD_NAME ${BOARD_TYPE})
endif()
target_compile_definitions(${COMPONENT_LIB}
                    PRIVATE BOARD_TYPE=\"${BOARD_TYPE}\" BOARD_NAME=\"${BOARD_NAME}\"
                    PRIVATE BUILTIN_TEXT_FONT=${BUILTIN_TEXT_FONT} BUILTIN_ICON_FONT=${BUILTIN_ICON_FONT}
                    )

# Add generation rules
add_custom_command(
    OUTPUT ${LANG_HEADER}
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_lang.py
            --language "${LANG_DIR}"
            --assets-dir "${STACKCHAN_RUNTIME_ASSETS_DIR}"
            --output "${LANG_HEADER}"
            ${LANG_SOUND_ARGS}
    DEPENDS
        ${LANG_JSON}
        ${STACKCHAN_ASSET_ARCHIVE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_lang.py
    COMMENT "Generating ${LANG_DIR} language config"
)

# Force build generation dependencies
add_custom_target(lang_header ALL
    DEPENDS ${LANG_HEADER}
)

# Find ESP-SR component dynamically
find_component_by_pattern("espressif__esp-sr" ESP_SR_COMPONENT ESP_SR_COMPONENT_PATH)
if(ESP_SR_COMPONENT_PATH)
    set(ESP_SR_MODEL_PATH "${ESP_SR_COMPONENT_PATH}/model")
endif()

if(NOT EXISTS "${STACKCHAN_FONTS_PATH}")
    message(FATAL_ERROR "Missing extracted stackchan_fonts assets: ${STACKCHAN_FONTS_PATH}")
endif()

set(DEFAULT_ASSET_FILES
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/app_center_bg.png"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_ai_agent.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_app_center.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_bat_lightning.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_bell.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_controller.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_dance.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_ezdata.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_home.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_indicator_left.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_indicator_right.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_sentinel.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_setup.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_wifi_high.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_wifi_low.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_wifi_medium.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/icon_wifi_slash.bin"
    "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin/setup_stackchan_front_view.bin"
)

# Function to build default assets based on configuration
function(build_default_assets_bin)
    # Set output path for generated assets.bin
    set(GENERATED_ASSETS_BIN "${CMAKE_BINARY_DIR}/generated_assets.bin")
    
    # Prepare arguments for build script
    set(BUILD_ARGS
        "--sdkconfig" "${SDKCONFIG}"
        "--output" "${GENERATED_ASSETS_BIN}"
    )
    
    # Add builtin text font if defined
    if(BUILTIN_TEXT_FONT)
        list(APPEND BUILD_ARGS "--builtin_text_font" "${BUILTIN_TEXT_FONT}")
    endif()
    
    # Add default emoji collection if defined
    if(DEFAULT_EMOJI_COLLECTION)
        list(APPEND BUILD_ARGS "--emoji_collection" "${DEFAULT_EMOJI_COLLECTION}")
    endif()
    
    foreach(DEFAULT_ASSET_FILE ${DEFAULT_ASSET_FILES})
        list(APPEND BUILD_ARGS "--extra_file" "${DEFAULT_ASSET_FILE}")
    endforeach()
    
    list(APPEND BUILD_ARGS "--esp_sr_model_path" "${ESP_SR_MODEL_PATH}")
    list(APPEND BUILD_ARGS "--stackchan_fonts_path" "${STACKCHAN_FONTS_PATH}")

    # Create custom command to build assets
    add_custom_command(
        OUTPUT ${GENERATED_ASSETS_BIN}
        COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_default_assets.py ${BUILD_ARGS}
        DEPENDS
            ${SDKCONFIG}
            ${STACKCHAN_ASSET_ARCHIVE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_default_assets.py
        COMMENT "Building default assets.bin based on configuration"
        VERBATIM
    )
    
    # Create target for generated assets
    add_custom_target(generated_default_assets ALL
        DEPENDS ${GENERATED_ASSETS_BIN}
    )
    
    # Set the generated file path in parent scope
    set(GENERATED_ASSETS_LOCAL_FILE ${GENERATED_ASSETS_BIN} PARENT_SCOPE)
    
    message(STATUS "Default assets build configured: ${GENERATED_ASSETS_BIN}")
endfunction()


# Function to get local assets file path.
function(get_assets_local_file assets_source assets_local_file_var)
    if(assets_source MATCHES "^https?://")
        message(FATAL_ERROR "Remote custom assets are not supported: ${assets_source}")
    endif()

    if(IS_ABSOLUTE "${assets_source}")
        set(ASSETS_LOCAL_FILE "${assets_source}")
    else()
        set(ASSETS_LOCAL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${assets_source}")
    endif()

    if(NOT EXISTS ${ASSETS_LOCAL_FILE})
        message(FATAL_ERROR "Assets file not found: ${ASSETS_LOCAL_FILE}")
    endif()

    message(STATUS "Using assets file: ${ASSETS_LOCAL_FILE}")
    set(${assets_local_file_var} ${ASSETS_LOCAL_FILE} PARENT_SCOPE)
endfunction()


partition_table_get_partition_info(size "--partition-name assets" "size")
partition_table_get_partition_info(offset "--partition-name assets" "offset")
if ("${size}" AND "${offset}")
    # Flash assets based on configuration
    if(CONFIG_FLASH_DEFAULT_ASSETS)
        # Build default assets based on configuration
        build_default_assets_bin()
        esptool_py_flash_to_partition(flash "assets" "${GENERATED_ASSETS_LOCAL_FILE}")
        message(STATUS "Generated default assets flash configured: ${GENERATED_ASSETS_LOCAL_FILE} -> assets partition")
    elseif(CONFIG_FLASH_CUSTOM_ASSETS)
        # Flash custom assets
        get_assets_local_file("${CONFIG_CUSTOM_ASSETS_FILE}" ASSETS_LOCAL_FILE)
        esptool_py_flash_to_partition(flash "assets" "${ASSETS_LOCAL_FILE}")
        message(STATUS "Custom assets flash configured: ${ASSETS_LOCAL_FILE} -> assets partition")
    elseif(CONFIG_FLASH_NONE_ASSETS)
        message(STATUS "Assets flashing disabled (FLASH_NONE_ASSETS)")
    endif()
else()
    message(STATUS "Assets partition not found, using v1 partition table")
endif()
