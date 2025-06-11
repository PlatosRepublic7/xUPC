# xUPC_ATC_Project/src/plugin/CMakeLists.txt

# Use variables from the top-level project
set(PLUGIN_TARGET_NAME "xUPC")
set(PLUGIN_FOLDER_NAME "xUPC")
set(PLUGIN_SIGNATURE "r.kitson.${PLUGIN_TARGET_NAME}")
set(PLUGIN_DESCRIPTION "UDP Server Plugin for X-Plane.")
set(PLUGIN_VERSION ${PROJECT_VERSION})

# --- X-Plane SDK Location ---
option(SDK_ROOT "Path to the X-Plane SDK" "")
if(NOT SDK_ROOT)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../SDK")
        set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../SDK" CACHE PATH "Path to the X-Plane SDK" FORCE)
    else()
        message(FATAL_ERROR "X-Plane SDK path not found. Please set SDK_ROOT.")
    endif()
endif()
message(STATUS "Using X-Plane SDK from: ${SDK_ROOT}")

# Create the plugin library target from its specific source file
add_library(${PLUGIN_TARGET_NAME} SHARED xudp.cpp)

# Link against the shared socket library
target_link_libraries(${PLUGIN_TARGET_NAME} PRIVATE shared_socket)

# Add SDK include directories
target_include_directories(${PLUGIN_TARGET_NAME} PRIVATE
    "${SDK_ROOT}/CHeaders/XPLM"
    "${SDK_ROOT}/CHeaders/Widgets"
)

# Add nlohmann-json include directory
# Add nlohmann-json to include in the project
target_include_directories(${PLUGIN_TARGET_NAME} PRIVATE "C:/SDKS/nlohmann-json/include")


# --- Platform Specific Settings & Naming (Largely unchanged from original) ---
if(WIN32)
    set(PLATFORM_SUBFOLDER "win_x64")
    set(PLATFORM_PLUGIN_FILENAME "xUPC.xpl")
    target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE WIN32=1 IBM=1)
    find_library(XPLM_MAIN_LIBRARY NAMES XPLM_64 PATHS "${SDK_ROOT}/Libraries/Win" NO_DEFAULT_PATH)
    if(NOT XPLM_MAIN_LIBRARY)
        message(FATAL_ERROR "XPLM_MAIN_LIBRARY not found.")
    endif()
    target_link_libraries(${PLUGIN_TARGET_NAME} PRIVATE "${XPLM_MAIN_LIBRARY}")
    set_target_properties(${PLUGIN_TARGET_NAME} PROPERTIES SUFFIX ".dll")

elseif(APPLE)
    # The macOS bundle logic remains the same as your original file 
    set(PLATFORM_SUBFOLDER "mac_x64")
    set(PLATFORM_PLUGIN_FILENAME "xUPC.xpl")
    target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE APL=1)
    set_target_properties(${PLUGIN_TARGET_NAME} PROPERTIES MACOSX_BUNDLE TRUE OUTPUT_NAME ${PLUGIN_TARGET_NAME})
    # (Add the rest of the MACOSX_BUNDLE property settings from your original file here)
    # The executable inside the bundle will be ${PLUGIN_TARGET_NAME}
    set(INFO_PLIST_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")

    # Create a default Info.plist.in if it doesn't exist
    if(NOT EXISTS ${INFO_PLIST_TEMPLATE})
        file(WRITE ${INFO_PLIST_TEMPLATE}
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
"<plist version=\"1.0\">\n"
"<dict>\n"
"    <key>CFBundleDevelopmentRegion</key><string>English</string>\n"
"    <key>CFBundleExecutable</key><string>${MACOSX_BUNDLE_EXECUTABLE_NAME}</string>\n"
"    <key>CFBundleGetInfoString</key><string>${PLUGIN_DESCRIPTION}</string>\n"
"    <key>CFBundleIdentifier</key><string>${PLUGIN_SIGNATURE}</string>\n"
"    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>\n"
"    <key>CFBundleName</key><string>${PLUGIN_TARGET_NAME}</string>\n"
"    <key>CFBundlePackageType</key><string>BNDL</string>\n"
"    <key>CFBundleShortVersionString</key><string>${PLUGIN_VERSION}</string>\n"
"    <key>CFBundleSignature</key><string>????</string>\n"
"    <key>CFBundleVersion</key><string>${PLUGIN_VERSION}</string>\n"
"    <key>CSResourcesFileMapped</key><true/>\n"
"</dict>\n"
"</plist>\n"
        )
        message(STATUS "Generated defualt Info.plist.in at ${INFO_PLIST_TEMPLATE}")
    endif()

elseif(UNIX AND NOT APPLE) # Linux
    set(PLATFORM_SUBFOLDER "lin_x64")
    set(PLATFORM_PLUGIN_FILENAME "xUPC.xpl")
    target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE LIN=1)
    set_target_properties(${PLUGIN_TARGET_NAME} PROPERTIES SUFFIX ".so")
else()
    message(FATAL_ERROR "Unsupported platform.")
endif()


# --- X-Plane Specific Compile Definitions (Unchanged) ---
target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE XPLM_API_DEPRECATED XPLM200=1 XPLM210=1 XPLM300=1 XPLM400=1)


# --- Common Compiler Flags (Same as client, but for the plugin target) ---
# --- Common Compiler Flags ---
if(MSVC)
    target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE WIN32_LEAN_AND_MEAN)
    target_compile_options(${PLUGIN_TARGET_NAME} PRIVATE 
        $<$<CONFIG:DEBUG>:/Od /Zi> # No optimizations, debug info
        $<$<CONFIG:RELEASE>:/O2 /DNDEBUG> # Optimization, define NDEBUG
        /W4 # W4 is a good general warning level for MSVC, similar to -Wall
        # MSVC doesn't have a direct equivalent for -Wextra
        # We can add /WX to treat warnings as errors, similar to -Werror
        # /WX
    )

    # Link ws2_32.lib for Windows Sockets
    target_link_libraries(${PLUGIN_TARGET_NAME} PRIVATE ws2_32)
else()
    # Assuming GCC/Clang or similar
    target_compile_options(${PLUGIN_TARGET_NAME} PRIVATE
        $<$<CONFIG:DEBUG>:-g -O0>           # Debug: enable debug symbols, no optimization
        $<$<CONFIG:RELEASE>:O2 -DNDEBUG>    # Release: optimization level 2, define NDEBUG
        -fvisibily=hidden
        -Wall
        -Wextra
        -Werror
    )
endif()


# --- Post-Build Command to Create Final Plugin Structure (Unchanged) ---
set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/${PLUGIN_FOLDER_NAME}/${PLATFORM_SUBFOLDER}")
add_custom_command(
    TARGET ${PLUGIN_TARGET_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PLUGIN_TARGET_NAME}>" "${OUTPUT_DIR}/${PLATFORM_PLUGIN_FILENAME}"
    COMMENT "Creating final plugin: ${OUTPUT_DIR}/${PLATFORM_PLUGIN_FILENAME}"
)