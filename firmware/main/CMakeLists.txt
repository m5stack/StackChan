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
# Select language directory according to Kconfig
if(CONFIG_LANGUAGE_ZH_CN)
    set(LANG_DIR "zh-CN")
elseif(CONFIG_LANGUAGE_ZH_TW)
    set(LANG_DIR "zh-TW")
elseif(CONFIG_LANGUAGE_EN_US)
    set(LANG_DIR "en-US")
elseif(CONFIG_LANGUAGE_JA_JP)
    set(LANG_DIR "ja-JP")
elseif(CONFIG_LANGUAGE_KO_KR)
    set(LANG_DIR "ko-KR")
elseif(CONFIG_LANGUAGE_VI_VN)
    set(LANG_DIR "vi-VN")
elseif(CONFIG_LANGUAGE_TH_TH)
    set(LANG_DIR "th-TH")
elseif(CONFIG_LANGUAGE_DE_DE)
    set(LANG_DIR "de-DE")
elseif(CONFIG_LANGUAGE_FR_FR)
    set(LANG_DIR "fr-FR")
elseif(CONFIG_LANGUAGE_ES_ES)
    set(LANG_DIR "es-ES")
elseif(CONFIG_LANGUAGE_IT_IT)
    set(LANG_DIR "it-IT")
elseif(CONFIG_LANGUAGE_RU_RU)
    set(LANG_DIR "ru-RU")
elseif(CONFIG_LANGUAGE_AR_SA)
    set(LANG_DIR "ar-SA")
elseif(CONFIG_LANGUAGE_HI_IN)
    set(LANG_DIR "hi-IN")
elseif(CONFIG_LANGUAGE_PT_PT)
    set(LANG_DIR "pt-PT")
elseif(CONFIG_LANGUAGE_PL_PL)
    set(LANG_DIR "pl-PL")
elseif(CONFIG_LANGUAGE_CS_CZ)
    set(LANG_DIR "cs-CZ")
elseif(CONFIG_LANGUAGE_FI_FI)
    set(LANG_DIR "fi-FI")
elseif(CONFIG_LANGUAGE_TR_TR)
    set(LANG_DIR "tr-TR")
elseif(CONFIG_LANGUAGE_ID_ID)
    set(LANG_DIR "id-ID")
elseif(CONFIG_LANGUAGE_UK_UA)
    set(LANG_DIR "uk-UA")
elseif(CONFIG_LANGUAGE_RO_RO)
    set(LANG_DIR "ro-RO")
elseif(CONFIG_LANGUAGE_BG_BG)
    set(LANG_DIR "bg-BG")
elseif(CONFIG_LANGUAGE_CA_ES)
    set(LANG_DIR "ca-ES")
elseif(CONFIG_LANGUAGE_DA_DK)
    set(LANG_DIR "da-DK")
elseif(CONFIG_LANGUAGE_EL_GR)
    set(LANG_DIR "el-GR")
elseif(CONFIG_LANGUAGE_FA_IR)
    set(LANG_DIR "fa-IR")
elseif(CONFIG_LANGUAGE_FIL_PH)
    set(LANG_DIR "fil-PH")
elseif(CONFIG_LANGUAGE_HE_IL)
    set(LANG_DIR "he-IL")
elseif(CONFIG_LANGUAGE_HR_HR)
    set(LANG_DIR "hr-HR")
elseif(CONFIG_LANGUAGE_HU_HU)
    set(LANG_DIR "hu-HU")
elseif(CONFIG_LANGUAGE_MS_MY)
    set(LANG_DIR "ms-MY")
elseif(CONFIG_LANGUAGE_NB_NO)
    set(LANG_DIR "nb-NO")
elseif(CONFIG_LANGUAGE_NL_NL)
    set(LANG_DIR "nl-NL")
elseif(CONFIG_LANGUAGE_SK_SK)
    set(LANG_DIR "sk-SK")
elseif(CONFIG_LANGUAGE_SL_SI)
    set(LANG_DIR "sl-SI")
elseif(CONFIG_LANGUAGE_SV_SE)
    set(LANG_DIR "sv-SE")
elseif(CONFIG_LANGUAGE_SR_RS)
    set(LANG_DIR "sr-RS")
endif()

# Define generation path
set(LANG_JSON "${STACKCHAN_RUNTIME_ASSETS_DIR}/locales/${LANG_DIR}/language.json")
set(LANG_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/assets/lang_config.h")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/assets")

# Collect current language audio files
file(GLOB LANG_SOUNDS ${STACKCHAN_RUNTIME_ASSETS_DIR}/locales/${LANG_DIR}/*.ogg)

# If not en-US, collect en-US audio files as fallback for missing files
if(NOT LANG_DIR STREQUAL "en-US")
    file(GLOB EN_US_SOUNDS ${STACKCHAN_RUNTIME_ASSETS_DIR}/locales/en-US/*.ogg)
    
    # Extract filenames (without path) from current language
    set(EXISTING_NAMES "")
    foreach(SOUND_FILE ${LANG_SOUNDS})
        get_filename_component(FILENAME ${SOUND_FILE} NAME)
        list(APPEND EXISTING_NAMES ${FILENAME})
    endforeach()
    
    # Only add en-US audio files that are missing in current language
    foreach(EN_SOUND ${EN_US_SOUNDS})
        get_filename_component(FILENAME ${EN_SOUND} NAME)
        if(NOT ${FILENAME} IN_LIST EXISTING_NAMES)
            list(APPEND LANG_SOUNDS ${EN_SOUND})
            message(STATUS "Using en-US fallback for missing audio: ${FILENAME}")
        endif()
    endforeach()
endif()

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

set(DEFAULT_ASSETS_EXTRA_FILES "${STACKCHAN_RUNTIME_ASSETS_DIR}/assets_bin")

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
    
    # Add default assets extra files if defined
    if(DEFAULT_ASSETS_EXTRA_FILES)
        list(APPEND BUILD_ARGS "--extra_files" "${DEFAULT_ASSETS_EXTRA_FILES}")
    endif()
    
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
