cmake_minimum_required(VERSION 3.15)
project(xUPC VERSION "1.0.0" LANGUAGES CXX)

# --- Standard C++ Settings ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- X-Plane SDK Location ---
# Option 1: The user provides the SDK path (via -DSDK_ROOT=...)
option(SDK_ROOT "Path to the X-Plane SDK" "")

if(NOT SDK_ROOT)
    # Option 2: Try to find it in common relative locations
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/SDK")
        set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/SDK" CACHE PATH "Path to the X-Plane SDK" FORCE)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../SDK")
        set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR/../SDK}" CACHE PATH "Path to the X-Plane SDK" FORCE)
    else()
        message(FATAL_ERROR "X-Plane SDK path not found. Please set SDK_ROOT (e.g. -DSDK_ROOT=)")
    endif()
endif()
message(STATUS "Using X-Plane SDK from: ${SDK_ROOT}")

# --- Plugin Information ---
set(PLUGIN_TARGET_NAME "xUPC")
set(PLUGIN_FOLDER_NAME "xUPC")
set(PLUGIN_SIGNATURE "r.kitson.${PLUGIN_TARGET_NAME}")
set(PLUGIN_DESCRIPTION "UDP Server Plugin for X-Plane.")
set(PLUGIN_VERSION ${PROJECT_VERSION})

# --- Source Files ---
add_library(${PLUGIN_TARGET_NAME} SHARED
    src/xudp.cpp
    src/UDPSocket.cpp
    src/UDPSocket.h
)
# Ensure headers in src/ can be found by .cpp files
target_include_directories(${PLUGIN_TARGET_NAME} PRIVATE src)

# Add SDK include directories specifically to the plugin target
target_include_directories(${PLUGIN_TARGET_NAME} PRIVATE
    "${SDK_ROOT}/CHeaders/XPLM"
    "${SDK_ROOT}/CHeaders/Widgets"
)

# Add nlohmann-json to include in the project
target_include_directories(${PLUGIN_TARGET_NAME} PRIVATE "C:/SDKS/nlohmann-json/include")

# --- Platform Specific Settings & Naming ---
# X-Plane expects plugin filenames like win.xpl, mac.xpl, lin.xpl inside a 64-bit subfolder.
if(WIN32)
    set(PLATFORM_SUBFOLDER "win_x64")
    set(PLATFORM_PLUGIN_FILENAME "xUPC.xpl")
    target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE WIN32=1 IBM=1)

    # --- Link X-Plane SDK Libraries ---
    set(XPLM_SDK_LIB_DIR "${SDK_ROOT}/Libraries/Win")

    find_library(XPLM_MAIN_LIBRARY
        NAMES XPLM_64 # Common names
        PATHS "${XPLM_SDK_LIB_DIR}"
        NO_DEFAULT_PATH)

    if(NOT XPLM_MAIN_LIBRARY)
        message(FATAL_ERROR "XPLM_MAIN_LIBRARY not found in ${XPLM_SDK_LIB_DIR}. Searched for XPLM_64.lib or XPLM.lib. Please check SDK_ROOT and SDK library path.")
    else()
        message(STATUS "Found XPLM Main Library: ${XPLM_MAIN_LIBRARY}")
        target_link_libraries(${PLUGIN_TARGET_NAME} PRIVATE "${XPLM_MAIN_LIBRARY}")
    endif()
    # --- End Link X-Plane libraries ---

    target_link_libraries(${PLUGIN_TARGET_NAME} PRIVATE ws2_32)
    set_target_properties(${PLUGIN_TARGET_NAME} PROPERTIES SUFFIX ".dll")
elseif(APPLE)
    set(PLATFORM_SUBFOLDER "mac_x64")
    set(PLATFORM_PLUGIN_FILENAME "xUPC.xpl")
    target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE APL=1)

    # macOS uses bundles for .xpl plugins
    # The bundle directory will be named mac.xpl (handled by install RENAME)
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

    set_target_properties(${PLUGIN_TARGET_NAME} PROPERTIES
        FRAMEWORK FALSE
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${INFO_PLIST_TEMPLATE}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${PLUGIN_SIGNATURE}"
        MAXOSX_BUNDLE_BUNDLE_NAME "${PLUGIN_TARGET_NAME}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PLUGIN_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PLUGIN_VERSION}"
        OUTPUT_NAME ${PLUGIN_TARGET_NAME}
    )
elseif(UNIX AND NOT APPLE) # Linux
    set(PLATFORM_SUBFOLDER "lin_x64")
    set(PLATFORM_PLUGIN_FILENAME "xUPC.xpl")
    target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE LIN=1)
    set_target_properties(${PLUGIN_TARGET_NAME} PROPERTIES SUFFIX ".so")
else()
    message(FATAL_ERROR "Unsupported platform for X-Plane plugin build.")
endif()

# --- X-Plane Specific Compile Definitions ---
target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE
    XPLM_API_DEPRECATED
    XPLM200=1
    XPLM210=1
    XPLM300=1
    XPLM301=1
    XPLM303=1
    XPLM400=1
    # We can adjust these flags based on the SDK we are targeting and the minimum X-Plane version
    # we want to support
)

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

# --- Post-Build Command to Create Final Plugin Structure ---
# This is a more direct approach. It runs after every build.
set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/${PLUGIN_FOLDER_NAME}/${PLATFORM_SUBFOLDER}")
add_custom_command(
    TARGET ${PLUGIN_TARGET_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PLUGIN_TARGET_NAME}>" "${OUTPUT_DIR}/${PLATFORM_PLUGIN_FILENAME}"
    COMMENT "Creating final plugin: ${OUTPUT_DIR}/${PLATFORM_PLUGIN_FILENAME}"
)

# --- Installation ---
# This step structures the output to be easily copied into X-Plane's plugins directory.


# --- Build Summary Messages ---
message(STATUS "X-Plane Plugin: ${PLUGIN_TARGET_NAME} (will be placed in folder: ${PLUGIN_FOLDER_NAME})")
message(STATUS "Signature: ${PLUGIN_SIGNATURE}")
message(STATUS "Version: ${PLUGIN_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE} (Use -DCMAKE_BUILD_TYPE=Release or Debug)")
message(STATUS "After install, the final plugin will be in: ${CMAKE_BINARY_DIR}/${PLUGIN_FOLDER_NAME}/${PLATFORM_SUBFOLDER}/")
message(STATUS "Copy the '${PLUGIN_FOLDER_NAME}' directory from there into X-Plane's Resources/plugins/ folder. ")