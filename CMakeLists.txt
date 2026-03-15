cmake_minimum_required(VERSION 3.20)

project(topaz_pan VERSION 0.2.0)

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

# Optional IDE grouping for cleaner structure
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    PluginProcessor.cpp
    PluginProcessor.h
    PluginEditor.cpp
    PluginEditor.h
)
