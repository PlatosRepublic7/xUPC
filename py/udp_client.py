import socket
import json
import os
import time

# Configuration
LISTEN_IP = "127.0.0.1"
LISTEN_PORT = 12345
BUFFER_SIZE = 4096  # This is to ensure we can receive the full JSON
CONSOLE_UPDATE_HZ = 5

UPDATE_INTERVAL_SECONDS = 1.0 / CONSOLE_UPDATE_HZ

def clear_console():
    """Clears the console screen"""
    os.system('cls' if os.name == 'nt' else 'clear')


def get_transponder_mode(mode_val):
    """Returns the string representation of the transponder mode"""
    if mode_val is None:
        return "UNK"
    modes = {
        0: "OFF",
        1: "STBY",
        2: "ON",
        3: "ALT",
        4: "TEST",
        5: "ALT_GND",
        6: "TA_ONLY",
        7: "TA_RA"
    }
    return modes.get(mode_val, "UNK")


def main():
    """
    Initialize a UDP socket to listen for X-Plane data,
    then enters a loop  to receive, parse, and display the data.
    """
    print(f"Starting UDP client, listening on {LISTEN_IP}:{LISTEN_PORT}")
    print(f"Console will update at {CONSOLE_UPDATE_HZ} Hz")

    # Create a UDP socket
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((LISTEN_IP, LISTEN_PORT))
        print("Socket created and bound successfully.")
    except Exception as e:
        print(f"Error creating or binding socket: {e}")
        return
    
    last_update_time = 0
    latest_aircraft_data = None
    
    try:
        while True:
            # The received data is in bytes, so we need to decode it into a string
            try:
                # Wait to receive data from socket
                data, addr = sock.recvfrom(BUFFER_SIZE)
                json_string = data.decode('utf-8')
                latest_aircraft_data = json.loads(json_string)
            except BlockingIOError:
                pass
            except (UnicodeDecodeError, json.JSONDecodeError) as e:
                # This might happen if a packet is corrupted or incomplete
                print(f"Could not decode packet: {e}")

            # Display Logic
            # Check if enough time has passed since the last screen update
            current_time = time.time()
            if current_time - last_update_time > UPDATE_INTERVAL_SECONDS:
                # Clear the console for a clean, updating display
                clear_console()

                # Print the received data
                print("--- X-Plane Data Received ---")
                if latest_aircraft_data:
                    # Using .get() provides a default if a key is missing
                    pos = latest_aircraft_data.get('position', {})
                    vec = latest_aircraft_data.get('vectors', {})

                    rad = latest_aircraft_data.get('radios', {})
                    transponder_mode_val = rad.get('transponder_mode')

                    transponder_mode = get_transponder_mode(transponder_mode_val)

                    # --- We need to convert certain datarefs into our desired units ---
                    # It's worth noting that alt_msl and alt_agl are in meters, and alt_msl double-precision floating point number
                    alt_agl = pos.get('alt_agl', 0.0)
                    alt_agl_ft = alt_agl * 3.28084

                    # Ground gs_kts is actually in meters per second, so we need to convert it to feet per min
                    gs_mps = vec.get('gs_kts', 0.0)
                    gs_kts = gs_mps * 1.94384

                    
                    
                    print('Position:')
                    print(f"    Lat:            {pos.get('lat', 0.0):.4f}")
                    print(f"    Lon:            {pos.get('lon', 0.0):.4f}")
                    print(f"    Alt (MSL):      {pos.get('alt_msl', 0.0):.1f} ft")
                    print(f"    Alt (AGL):      {alt_agl_ft:.1f} ft")
                    print('\nVectors:')
                    print(f"    IAS:            {vec.get('ias_kts', 0.0):.1f} kts")
                    print(f"    GS:             {gs_kts:.1f} kts")
                    print(f"    Heading (True): {vec.get('hdg_true', 0.0):.1f} deg")
                    print(f"    Heading (Mag):  {vec.get('hdg_mag', 0.0):.1f} deg")
                    print(f"    V/S:            {vec.get('vs_fpm', 0.0):.0f} fpm")
                    print('\nRadios:')
                    print(f"    Transponder:    {rad.get('transponder_code', '----')}  ({transponder_mode})")
                    print(f"    Ident:          {rad.get('transponder_ident', 'False')}")

                    print('\n---------------------------')
                else:
                    print("\nWaiting for first data packet from X-Plane...")

                # Reset the timer
                last_update_time = current_time
            time.sleep(0.01)
            
    except KeyboardInterrupt:
        print("\nClient shutting down.")
    finally:
        sock.close()
        print('Socket closed.')

if __name__ == "__main__":
    main()