add_executable(atc_client main.cpp)

# Link the client against our shared socket library
target_link_libraries(atc_client PRIVATE shared_socket)

# Add include directory for the nlohmann-json dependency
target_include_directories(atc_client PRIVATE "C:/SDKS/nlohmann-json/include")

# --- Common Compiler Flags (mirrors plugin setup) ---
if(MSVC)
    target_compile_options(atc_client PRIVATE
        $<$<CONFIG:DEBUG>:/Od /Zi>
        $<$<CONFIG:RELEASE>: /O2 /DNDEBUG>
        /W4
    )
else()
    target_compile_options(atc_client PRIVATE
        $<$<CONFIG:DEBUG>:-g -O0>
        $<$<CONFIG:RELEASE>:-O2 -DNDEBUG>
        -Wall
        -Wextra
    )
endif()