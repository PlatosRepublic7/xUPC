#pragma once
#include <string>

#ifdef _WIN32
    #ifndef _WINSOCKAPI_
        #define _WINSOCKAPI_
    #endif
    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
#else // POSIX
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    #include <fcntl.h>
#endif

class UDPSocket {
public:
    UDPSocket();
    ~UDPSocket();

    // Initialize Method:
    // destination_ip: IP to send data to
    // destination_port: Port to send data to
    // local_receive_port: Optional. If > 0, the socket will also be bound to this local port
    //                     for receiving messages. If 0 or negative, it's send-only
    bool initialize(const std::string& destination_ip, int destination_port, int local_receive_port = 0);

    bool send(const char* data, int length);

    // Receive Method:
    // buffer: Pointer to a character array where incoming data will be stored
    // buffer_length: Maximum number of bytes to read into the buffer
    // out_sender_ip: (Output) String to store the IP address of the datagram sender
    // out_sender_port: (Output) Integer to store the port of the datagram sender
    // Returns:
    //      > 0: Number of bytes received.
    //      0: Na data received (if non-blocking and no data is available)
    //      -1: An error occured
    //      -2: Socket not initialized for receiving or error specific to non-blocking
    int receive(char* buffer, int buffer_length, std::string& out_sender_ip, int& out_sender_port);

    void cleanup();
    bool isInitialized() const;

private:
    bool set_non_blocking(bool enabled);

#ifdef _WIN32
    SOCKET sock_fd;
    WSADATA wsa_data;
#else
    int sock_fd;
#endif
    // For sending:
    struct sockaddr_in destination_addr;
    int bound_local_receive_port;

    bool initialized;
    bool receive_initialized;
};