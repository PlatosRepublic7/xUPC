# Create a static library named 'shared_socket'
add_library(shared_socket STATIC
    UDPSocket.cpp
    UDPSocket.h
)

# Any target linking this library should also be able to find its headers
target_include_directories(shared_socket PUBLIC .)

# Add platform specific libraries that are required by this shared code.
# The 'PUBLIC' keyword ensures that any target linking against shared_socket will also get these
# link libraries automatically
if(WIN32)
    target_link_libraries(shared_socket PUBLIC ws2_32)
endif()