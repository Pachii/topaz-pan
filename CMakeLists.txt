cmake_minimum_required(VERSION 3.20)

project(topaz_pan VERSION 0.3.0)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# ==============================================================================
# Fetch JUCE automatically to make the project self-contained
# ==============================================================================
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
)
FetchContent_MakeAvailable(juce)

# ==============================================================================
# Create the plugin target
# ==============================================================================
juce_add_plugin(AbstractWidener
    COMPANY_NAME              "topaz"
    BUNDLE_ID                 "com.topaz.topaz-pan"
    PLUGIN_MANUFACTURER_CODE  "Anti"
    PLUGIN_CODE               "Widn"
    FORMATS                   VST3 AU Standalone
    PRODUCT_NAME              "topaz pan"
)

# Target dependencies
target_link_libraries(AbstractWidener
    PRIVATE
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Fix macOS 15.0 SDK CoreGraphics error in JUCE 8.0.0
target_compile_definitions(AbstractWidener PUBLIC JUCE_DISABLE_CGIMAGE_DEPRECATION_WARNINGS=1 JUCE_COREGRAPHICS_DRAW_ASYNC=1)

# Source files
target_sources(AbstractWidener
    PRIVATE
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
)

# Generate <JuceHeader.h> since our code relies on it
juce_generate_juce_header(AbstractWidener)

target_compile_features(AbstractWidener PUBLIC cxx_std_20)

target_compile_definitions(AbstractWidener PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

if(APPLE)
    function(ad_hoc_sign_bundle target_name)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND /bin/sh -c "codesign --force --deep --sign - --timestamp=none \"$<TARGET_FILE_DIR:${target_name}>/../..\""
            VERBATIM
        )
    endfunction()

    ad_hoc_sign_bundle(AbstractWidener_Standalone)
    ad_hoc_sign_bundle(AbstractWidener_VST3)
    ad_hoc_sign_bundle(AbstractWidener_AU)
endif()

# Optional IDE grouping for cleaner structure
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    PluginProcessor.cpp
    PluginProcessor.h
    PluginEditor.cpp
    PluginEditor.h
)
