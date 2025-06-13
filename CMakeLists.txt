cmake_minimum_required(VERSION 3.15)
project(xUPC_ATC_Suite VERSION "1.0.0" LANGUAGES CXX)

# --- Standard C++ Settings (Applied Globally) ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Fetch SQLite and SQLiteCpp ---
include(FetchContent)

FetchContent_Declare(
  SQLiteCpp
  GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
  GIT_TAG        3.3.0 # Or another stable version
)

# For Visual Studio/MSVC, this helps prevent runtime library mismatch errors.
if(MSVC)
    set(SQLITECPP_STATIC_RUNTIMELIBRARY_FLAG "/MT")
endif()

# Tell SQLiteCpp to download SQLite for us.
set(SQLITECPP_FETCH_SQLITE ON CACHE BOOL "")

# Disable the cpplint style checker which causes issues on Windows
if(WIN32)
  set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "")
endif()

# This makes the SQLiteCpp target available to our project
FetchContent_MakeAvailable(SQLiteCpp)

# Add subdirectories. CMake will find and process the CMakeLists.txt file in each one
add_subdirectory(src/shared)
add_subdirectory(src/plugin)
add_subdirectory(src/client)