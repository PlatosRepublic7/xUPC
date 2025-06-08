#include "UDPSocket.h"
#include <stdexcept>
#include <cstring>
#include <string>


#if __has_include("XPLMUtilities.h") && __has_include("XPLMDefs.h")
    #define HAS_XPLM_HEADERS 1
    #include "XPLMUtilities.h"
#else
    #define HAS_XPLM_HEADERS 0
    #include <iostream>
#endif


static void LogUDPError(const std::string& msg) {
#if HAS_XPLM_HEADERS
    XPLMDebugString(("UDPSocket Error: " + msg + "\n").c_str());
#else
    std::cerr << "UDPSocket Error: " << msg << std::endl;
#endif
}


static void LogUDPInfo(const std::string& msg) {
    #if HAS_XPLM_HEADERS
    XPLMDebugString(("UDPSocket Info: " + msg + "\n").c_str());
#else
    std::cerr << "UDPSocket Info: " << msg << std::endl;
#endif
}


UDPSocket::UDPSocket() : initialized(false), receive_initialized(false), bound_local_receive_port(0) {
#ifdef _WIN32
    sock_fd = INVALID_SOCKET;
#else
    sock_fd = -1;
#endif
    std::memset(&destination_addr, 0, sizeof(destination_addr));
}


UDPSocket::~UDPSocket() {
    cleanup();
}


bool UDPSocket::isInitialized() const {
    return initialized;
}


bool UDPSocket::set_non_blocking(bool enabled) {
#ifdef _WIN32
    if (!initialized || sock_fd == INVALID_SOCKET) {
        LogUDPError("Cannot set non-blocking, socket not valid.");
        return false;
    }
    u_long mode = enabled ? 1 : 0;
    if (ioctlsocket(sock_fd, FIONBIO, &mode) != NO_ERROR) {
        LogUDPError("ioctlsocket failed to set non-blocking mode (Windows). Error: " + std::to_string(WSAGetLastError()));
        return false;
    }
#else // POSIX
    if(!initialized || sock_fd < 0) {
        LogUPDError("Cannot set non-blocking, socket not valid.");
        return false;
    }
    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (flags == -1) {
        LogUDPError("fcntl(F_GETFL) failed (POSIX). errno: " + std::to_string(errno) + " (" + strerror(errno) + ")");
        return false;
    }
    flags = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fnctl(sock_fd, F_SETFL, flags) == -1) {
        LogUDPError("fnctl(F_SETFL) failed to set non-blocking mode (POSIX). errno: " + std::to_string(errno) + " (" + strerror(errno) + ")");
        return false;
    }
#endif
    LogUDPInfo(std::string("Socket non-blocking mode ") + (enabled ? "enabled." : "disabled."));
    return true;
}


bool UDPSocket::initialize(const std::string& destination_ip, int destination_port, int local_receive_port) {
    if (initialized) {
        LogUDPInfo("UDPSocket already initialized. Cleaning up previous state.");
        cleanup();
    }
    bound_local_receive_port = 0;

#ifdef _WIN32
    int startup_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup_result != 0) {
        LogUDPError("WSAStartup failed. Error Code: " + std::to_string(startup_result));
        return false;
    }
#endif

    sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

#ifdef _WIN32
    if (sock_fd == INVALID_SOCKET) {
        LogUDPError("Socket creation failed (Windows). Error Code: " + std::to_string(WSAGetLastError()));
        WSACleanup();
        return false;
    }
#else
    if (sock_fd < 0) {
        LogUDPError("Socket creation failed (POSIX). errno: " + std::to_string(errno) + " (" + strerror(errno) + ")");
        return false;
    }
#endif

    // Configure destination address for sending
    destination_addr.sin_family = AF_INET;
    destination_addr.sin_port = htons(static_cast<u_short>(destination_port));
    int pton_result_dest = inet_pton(AF_INET, destination_ip.c_str(), &destination_addr.sin_addr);

    if (pton_result_dest <= 0) {
#ifdef _WIN32
        LogUDPError(std::string("inet_pton failed for destination IP (Windows). ") + std::to_string(WSAGetLastError()));
        closesocket(sock_fd);
        WSACleanup();
#else
        LogUDPError(std::string("inet_pton failed for destination IP (POSIX). errno: ") + strerror(errno));
        close(sock_fd);
#endif
        sock_fd = -1;
        return false;
    }

    initialized = true; // Socket created, destination address for sending is set up

    // --- Setup for receiving (if local_receive_port is specified) ---
    if (local_receive_port > 0) {
        struct sockaddr_in local_addr;
        std::memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = htons(static_cast<u_short>(local_receive_port));
        local_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to any available local IP address

        // Bind the socket to the local address and port
        if (bind(sock_fd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
#ifdef _WIN32
            LogUDPError("bind failed (Windows). Error Code: " + std::to_string(WSAGetLastError()));
#else
            LogUDPError("bind failed (POSIX). errno: " + std::to_string(errno) + " (" + strerror(errno) + ")");
#endif
            // Don't entirely fail initialize if only bind fails, could still be send-only
            // Or, decide to fail completely:
            // cleanup();
            // return false;
            receive_initialized = false; // Mark as not ready for receiving
            LogUDPInfo("Socket SENDING initialized, but RECEIVING on port " + std::to_string(local_receive_port) + " FAILED (bind).");
        } else {
            // Successfully bound, now set to non-blocking for receive operations
            if (!set_non_blocking(true)) {
                LogUDPError("Failed to set socket to non-blocking mode. Receiving might block X-Plane!");
                // Decide if this is fatal for receive initialization
                receive_initialized = false; // Or true but with a warning
            } else {
                receive_initialized = true;
                bound_local_receive_port = local_receive_port;
                LogUDPInfo("Socket initialized for SENDING to " + destination_ip + ":" + std::to_string(destination_port) + 
                           " and RECEIVING on port " + std::to_string(local_receive_port));
            }
        }
    } else {
        receive_initialized = false; // Not attempting to receive
        LogUDPInfo("Socket initialized for SENDING ONLY to " + destination_ip + ":" + std::to_string(destination_port));
    }

    return initialized; // Overall success if sending part is OK
}


bool UDPSocket::send(const char* data, int length) {
    if (!initialized) {
        LogUDPError("Cannot send, UDPSocket not initialized for sending.");
        return false;
    }
#ifdef _WIN32
    if (sock_fd == INVALID_SOCKET) { /* error */ return false; }
#else
    if (sock_fd < 0) { /* error */ return false; }
#endif

    int bytes_sent = sendto(sock_fd, data, length, 0, (struct sockaddr*)&destination_addr, sizeof(destination_addr));

#ifdef _WIN32
    if (bytes_sent == SOCKET_ERROR) {
        LogUDPError("sendto failed (Windows). Error Code: " + std::to_string(WSAGetLastError()));
        return false;
    }
#else
    if (bytes_sent < 0) {
        LogUDPError("sendto failed (POSIX). errno: " + std::to_string(errno) + " (" + strerror(errno) + ")");
        return false;
    }
#endif
    // Optional: We could check if bytes_sent != length
    return true;
}


int UDPSocket::receive(char* buffer, int buffer_length, std::string& out_sender_ip, int& out_sender_port) {
    if (!receive_initialized) {
        LogUDPError("Cannot receive, UDPSocket not initialized for receiving or bind failed.");
        return -2;
    }

#ifdef _WIN32
    if (sock_fd == INVALID_SOCKET) {
        return -2;
    }
#else
    if (sock_fd < 0) {
        return -2;
    }
#endif

    struct sockaddr_in sender_addr;
    socklen_t sender_addr_len = sizeof(sender_addr);
    std::memset(&sender_addr, 0, sizeof(sender_addr));

    // recvfrom is used to receive data on a connectionless socket.
    // It also provides the address of the sender.
    int bytes_received = recvfrom(sock_fd, buffer, buffer_length, 0, (struct sockaddr*)&sender_addr, &sender_addr_len);

    if (bytes_received < 0) {   // Error occurred
#ifdef _WIN32
        int error_code = WSAGetLastError();
        if (error_code == WSAEWOULDBLOCK) {
            return 0; // Non-blocking socket, no data available to read
        }
        LogUDPError("recvfrom failed (Windows). Error Code: " + std::to_string(error_code));
#else // POSIX
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return 0; // Non-blocking socket, no data to read
        }
        LogUDPError("recvfrom failed (POSIX). errno: " + std::to_string(errno) + " (" + strerror(errno) + ")");
#endif
        return -1; // General error
    } else if (bytes_received == 0) {
        // For UDP, receiving 0 bytes is unusual unless the buffer_length was 0.
        // Some implementations might return 0 if the peer performed a shutdown,
        // but this is less defined for UDP than TCP/
        // We'll treat it as "no actual data payload"
        return 0;
    } else {
        // Data received successfully, extract sender's IP and port
        char sender_ip_str[INET_ADDRSTRLEN]; // INET_ADDRSTRLEN for IPv4
        inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip_str, INET_ADDRSTRLEN);
        out_sender_ip = sender_ip_str;
        out_sender_port = ntohs(sender_addr.sin_port); // Conver port back from network to host in byte order
    }

    return bytes_received;
}


void UDPSocket::cleanup() {
    bool was_initialized_at_all = initialized || receive_initialized;

#ifdef _WIN32
    if (sock_fd != INVALID_SOCKET) {
        closesocket(sock_fd);
        sock_fd = INVALID_SOCKET;
    }
    if (initialized || receive_initialized) {
        // Only call WSACleanup if WSAStartup was likely called
        WSACleanup();
    }
#else
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
#endif
    initialized = false;
    receive_initialized = false;
    bound_local_receive_port = 0;
    if (was_initialized_at_all) LogUDPInfo("UDPSocket cleaned up.");
}