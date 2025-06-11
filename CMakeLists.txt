cmake_minimum_required(VERSION 3.15)
project(xUPC_ATC_Suite VERSION "1.0.0" LANGUAGES CXX)

# --- Standard C++ Settings (Applied Globally) ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add subdirectories. CMake will find and process the CMakeLists.txt file in each one
add_subdirectory(src/shared)
add_subdirectory(src/plugin)
add_subdirectory(src/client)