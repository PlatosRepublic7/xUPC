#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include "UDPSocket.h"
#include "nlohmann/json.hpp"
#include "NavDataManager.h"

using json = nlohmann::json;

void clear_console();
std::string get_transponder_code(int mode_val);
void live_flight_data(UDPSocket& client_socket, const char* LISTEN_IP, int LISTEN_PORT);

int main() {
    // --- Configuration and Socket Data ---
    const char* LISTEN_IP = "127.0.0.1";
    int LISTEN_PORT = 12345;
    UDPSocket client_socket;

    // Initialize and build the navigation database
    try {
        const std::string db_path = "nav_data.db";
        const std::string xplane_root = "C:/X-Plane 12";
        NavDataManager nav_db(db_path);

        std::cout << "Updating Navigation Database..." << std::endl;
        nav_db.update_database(xplane_root);

        // Test airport query
        std::cout << "--- TEST AIRPORT QUERY ---" << std::endl;

        std::string q_icao = "KEWR";
        if (auto airport = nav_db.get_airport(q_icao)) {
            // .value() lets you access the json object inside the optional
            std::cout << "Found " << q_icao << ": " << airport.value().dump(2) << std::endl;
        } else {
            std::cout << "Airport " << q_icao << " not found." << std::endl;
        }

        
    } catch (std::exception& e) {
        std::cerr << "A critical error occured during database setup: " << e.what() << std::endl;
    }
    
    //live_flight_data(client_socket, LISTEN_IP, LISTEN_PORT);
    

    return 0;
}

void clear_console() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

std::string get_transponder_mode(int mode_val) {
    switch (mode_val) {
        case 0: return "OFF";
        case 1: return "STBY";
        case 2: return "ON";
        case 3: return "ALT";
        case 4: return "TEST";
        case 5: return "ALT_GND";
        case 6: return "TA_ONLY";
        case 7: return "TA_RA";
        default: return "UNK";
    }
}

void live_flight_data(UDPSocket& client_socket, const char* LISTEN_IP, int LISTEN_PORT) {
    /*
        This is the function that runs the real-time UDP data stream for flight data
    */
    const int BUFFER_SIZE = 4096;

    // Initialize the client_socket
    if (!client_socket.initialize(LISTEN_IP, 1, LISTEN_PORT)) {
        std::cerr << "Error: Failed to initialize and bind socket." << std::endl;
    }

    std::cout << "Socket created and bound successfully. Waiting for data..." << std::endl;

    auto last_update_time = std::chrono::steady_clock::now();
    const auto UPDATE_INTERVAL = std::chrono::milliseconds(1000 / 5);
    json latest_aircraft_data;

    while (true) {
        try {
            char buffer[BUFFER_SIZE];
            std::memset(buffer, 0, BUFFER_SIZE);

            std::string sender_ip;
            int sender_port;
            int bytes_received = client_socket.receive(buffer, BUFFER_SIZE - 1, sender_ip, sender_port);

            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';

                latest_aircraft_data = json::parse(buffer);

                // --- Display Logic ---
                auto current_time = std::chrono::steady_clock::now();
                if (current_time - last_update_time > UPDATE_INTERVAL) {
                    clear_console();
                std::cout << "--- X-Plane Data Received ---" << std::endl;

                if (!latest_aircraft_data.empty()) {
                    // --- Data Extraction and Conversion ---
                    json pos = latest_aircraft_data.value("position", json::object());
                    json vec = latest_aircraft_data.value("vectors", json::object());
                    json rad = latest_aircraft_data.value("radios", json::object());

                    double alt_agl_m = pos.value("alt_agl", 0.0);
                    double alt_agl_ft = alt_agl_m * 3.28084;

                    double gs_mps = vec.value("gs_kts", 0.0);
                    double gs_kts = gs_mps * 1.94384;

                    // --- Printing ---
                    std::cout << "Position:" << std::endl;
                    printf("    Lat:            %.4f\n", pos.value("lat", 0.0));
                    printf("    Lon:            %.4f\n", pos.value("lon", 0.0));
                    printf("    Alt (MSL):      %.1f ft\n", pos.value("alt_msl", 0.0));
                    printf("    Alt (AGL):      %.1f ft\n", alt_agl_ft);

                    std::cout << "\nVectors:" << std::endl;
                    printf("    IAS:            %.1f kts\n", vec.value("ias_kts", 0.0));
                    printf("    GS:             %.1f kts\n", gs_kts);
                    printf("    Heading (True): %.1f deg\n", vec.value("hdg_true", 0.0));
                    printf("    Heading (Mag):  %.1f deg\n", vec.value("hdg_mag", 0.0));
                    printf("    V/S:            %.0f fpm\n", vec.value("vs_fpm", 0.0));

                    std::cout << "\nRadios:" << std::endl;
                    printf("    Transponder:    %d (%s)\n", rad.value("transponder_code", 0), get_transponder_mode(rad.value("transponder_mode", 0)).c_str());
                    printf("    Ident:          %d\n", rad.value("transponder_ident", 0));

                    std::cout << "\n---------------------------" << std::endl;

                } else {
                    std::cout << "\nWaiting for first data packet from X-Plane..." << std::endl;
                }
                    last_update_time = current_time;
                }
            }
        } 
        // Catch specific exceptions to get more detailed error info.
        catch (const json::parse_error& e) {
            // This will tell you exactly what went wrong with the JSON packet.
            std::cerr << "JSON Parse Error: " << e.what() << std::endl;
            // We 'continue' the loop instead of exiting.
            continue; 
        } 
        catch (const std::exception& e) {
            std::cerr << "An error occurred: " << e.what() << std::endl;
        }

        // Small sleep to prevent the loop from consuming 100% CPU if data flow stops.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    client_socket.cleanup();
    std::cout << "Socket closed." << std::endl;
}