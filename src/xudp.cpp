#include "UDPSocket.h"
#include "nlohmann/json.hpp" // Include the json library for serialization

// X-Plane SDK headers
#define XPLM_API_DEPRECATED 1 // For XPLMDebugString if used, and older API's
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMDataAccess.h"
#include "XPLMProcessing.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h" // For XPLMDebugString, XPLMGetSystemPath

#include <string>
#include <vector>
#include <cstring>

// For convenience
using json = nlohmann::json;

// Configuration for  UDP
static UDPSocket g_udp_socket;
const char* PYTHON_CLIENT_IP = "127.0.0.1";
int PYTHON_CLIENT_PORT = 12345;
float SEND_INTERVAL_SECONDS = 1.0f / 30.0f; // Send data 30 times per second

// DataRefs
static XPLMDataRef g_lat_ref = NULL;
static XPLMDataRef g_lon_ref = NULL;
static XPLMDataRef g_indicated_alt_ft_ref = NULL;
static XPLMDataRef g_alt_agl_ref = NULL;
static XPLMDataRef g_ias_ref = NULL;
static XPLMDataRef g_gs_ref = NULL;
static XPLMDataRef g_heading_true_ref = NULL;
static XPLMDataRef g_heading_mag_psi_ref = NULL;
static XPLMDataRef g_vs_fpm_ref = NULL;

// Transponder DataRefs
static XPLMDataRef g_transponder_mode_ref = NULL;
static XPLMDataRef g_transponder_code_ref = NULL;
static XPLMDataRef g_transponder_ident_ref = NULL;

// Flight loop callback function
float MyFlightLoopCallback(
    float /*inElapsedSinceLastCall*/,
    float /*inElapsedTimeSinceLastFlightLoop*/,
    int   /*inCounter*/,
    void* /*inRefcon*/) {

    if (g_udp_socket.isInitialized()) {
        // Create a JSON object to hold our data
        json aircraft_data;

        // Populate the JSON object by reading the DataRefs
        aircraft_data["position"] = {
            {"lat",     XPLMGetDataf(g_lat_ref)},
            {"lon",     XPLMGetDataf(g_lon_ref)},
            {"alt_msl", XPLMGetDatad(g_indicated_alt_ft_ref)},
            {"alt_agl", XPLMGetDataf(g_alt_agl_ref)}
        };

        aircraft_data["vectors"] = {
            {"ias_kts",     XPLMGetDataf(g_ias_ref)},
            {"gs_kts",      XPLMGetDataf(g_gs_ref)},
            {"hdg_true",    XPLMGetDataf(g_heading_true_ref)},
            {"hdg_mag",     XPLMGetDataf(g_heading_mag_psi_ref)},
            {"vs_fpm",      XPLMGetDataf(g_vs_fpm_ref)}
        };

        aircraft_data["radios"] = {
            {"transponder_code",    XPLMGetDatai(g_transponder_code_ref)},
            {"transponder_mode",    XPLMGetDatai(g_transponder_mode_ref)},
            {"transponder_ident",   XPLMGetDatai(g_transponder_ident_ref)}
        };

        // Serialize the JSON object into a string
        std::string json_string = aircraft_data.dump();

        // Send the data over UDP
        g_udp_socket.send(json_string.c_str(), json_string.length());
    }

    return SEND_INTERVAL_SECONDS; // Request next call after this interval
}


PLUGIN_API int XPluginStart(
    char* outName,
    char* outSig,
    char* outDesc) {
    
    XPLMDebugString("xUPC: XPlugin Start called.\n");

    // Plugin Info - use std::string and strncpy for safety
    std::string plugin_name = "xUPC";
    std::string plugin_sig = "r.kitson.xUPC";
    std::string plugin_desc = "UDP server that sends/receives data for external applications.";

    strncpy_s(outName, 256, plugin_name.c_str(), _TRUNCATE);
    strncpy_s(outSig, 256, plugin_sig.c_str(), _TRUNCATE);
    strncpy_s(outDesc, 256, plugin_desc.c_str(), _TRUNCATE);

    // Initialize UDPSocket
    if (!g_udp_socket.initialize(PYTHON_CLIENT_IP, PYTHON_CLIENT_PORT)) {
        XPLMDebugString("xUPC: FATAL - Failed to initialize UDP Socket.\n");
        return 0;
    }
    XPLMDebugString("xUPC: UDP Socket Initialized Successfully.\n");

    // Find all our DataRefs
    XPLMDebugString("xUPC: Finding DataRefs...\n");
    g_lat_ref = XPLMFindDataRef("sim/flightmodel/position/latitude");
    g_lon_ref = XPLMFindDataRef("sim/flightmodel/position/longitude");
    g_indicated_alt_ft_ref = XPLMFindDataRef("sim/cockpit2/gauges/indicators/alititude_ft_pilot");   // This is so we can get an alt while not having to do the weather compensation ourselves
    g_alt_agl_ref = XPLMFindDataRef("sim/flightmodel/position/y_agl");                      // This is in meters
    g_ias_ref = XPLMFindDataRef("sim/flightmodel/position/indicated_airspeed2");            // v8.10.0+
    g_gs_ref = XPLMFindDataRef("sim/flightmodel/position/groundspeed");                     // This is in meters per second
    g_heading_true_ref = XPLMFindDataRef("sim/flightmodel/position/true_psi");              // Heading of the aircraft relative to the earth below the aircraft - true degrees north, always
    g_heading_mag_psi_ref = XPLMFindDataRef("sim/flightmodel/position/mag_psi");            // Real magnetic heading of the aircraft
    g_vs_fpm_ref = XPLMFindDataRef("sim/flightmodel/position/vh_ind_fpm");

    // Get transponder DataRefs --- Using "modern" DataRefs
    g_transponder_mode_ref = XPLMFindDataRef("sim/cockpit2/radios/actuators/transponder_mode");
    g_transponder_code_ref = XPLMFindDataRef("sim/cockpit2/radios/actuators/transponder_code");
    g_transponder_ident_ref = XPLMFindDataRef("sim/cockpit2/radios/indicators/transponder_id");

    // It's good practice to check if they were all found
    if (!g_lat_ref || !g_lon_ref || !g_indicated_alt_ft_ref || !g_alt_agl_ref || !g_ias_ref || !g_gs_ref || !g_heading_true_ref || !g_vs_fpm_ref || !g_transponder_code_ref || !g_transponder_mode_ref) {
        XPLMDebugString("xUPC: WARNING - One or more DataRefs were not found. Data will be incomplete.\n");
    } else {
        XPLMDebugString("xUPC: All DataRefs found successfully.\n");
    }

    // Register flight loop callback
    XPLMRegisterFlightLoopCallback(
        MyFlightLoopCallback,
        SEND_INTERVAL_SECONDS,
        NULL);
    XPLMDebugString("xUPC: Flight loop callback registered.\n");

    XPLMDebugString("xUPC: XPluginStart completed successfully.\n");
    return 1;
}


PLUGIN_API void XPluginStop(void) {
    XPLMDebugString("xUPC: XPluginStop called.\n");
    XPLMUnregisterFlightLoopCallback(MyFlightLoopCallback, NULL);
    g_udp_socket.cleanup();
    XPLMDebugString("xUPC: XPluginStop completed.\n");
}


PLUGIN_API void XPluginDisable(void) {

}


PLUGIN_API int XPluginEnable(void) {
    return 1;
}


PLUGIN_API void XPluginReceiveMessage(XPLMPluginID /*inFromWho*/, intptr_t /*inMessage*/, void* /*inParam*/){

}