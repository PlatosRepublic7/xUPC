#include "NavDataManager.h"
#include "LookaheadLineReader.h"
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <sqlite3.h>
#include <SQLiteCpp/Transaction.h>
#include <SQLiteCpp/Statement.h>

// Alias for the std::filesystem namespace
namespace fs = std::filesystem;

// Type alias for 

// Constructor
NavDataManager::NavDataManager(const std::string& db_path) : db(db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) {
    std::cout << "Database opened at " << db.getFilename() << std::endl;

    // Call the method to set up tables
    create_tables();
}

// Create database tables
void NavDataManager::create_tables() {
    try {
        // We use db.exec() for commands that don't return data.
        db.exec(R"sql(
            CREATE TABLE IF NOT EXISTS airports (
                icao TEXT PRIMARY KEY,
                iata TEXT,
                faa TEXT,
                airport_name TEXT,
                elevation INTEGER,
                type TEXT,
                latitude REAL,
                longitude REAL,
                country TEXT,
                city TEXT,
                region TEXT,
                transition_alt INTEGER,
                transition_level INTEGER
            );
            CREATE TABLE IF NOT EXISTS runways (
                runway_id INTEGER PRIMARY KEY AUTOINCREMENT,
                airport_icao TEXT,
                width REAL,
                surface INTEGER,
                end1_rw_number TEXT NOT NULL,
                end1_lat REAL,
                end1_lon REAL,
                end1_d_threshold REAL,
                end1_rw_marking_code INTEGER,
                end1_rw_app_light_code INTEGER,
                end2_rw_number TEXT NOT NULL,
                end2_lat REAL,
                end2_lon REAL,
                end2_d_threshold REAL,
                end2_rw_marking_code INTEGER,
                end2_rw_app_light_code INTEGER,

                UNIQUE (airport_icao, end1_rw_number, end2_rw_number),
                FOREIGN KEY (airport_icao) REFERENCES airports (icao)
            );
        )sql");
        // NOTE: The r"sql(...)sql" syntax is a C++ raw string literal.
        // It's useful for writing multi-line SQL queries without escaping characters
    } catch (const std::exception& e){
        std::cerr << "SQLite error in create_tables: " << e.what() << std::endl;
        throw; // Re-throw exception to notifiy the caller
    }
}

std::vector<fs::path> NavDataManager::find_all_apt_dat_files(const fs::path& xplane_root_path) {
    std::vector<fs::path> all_apt_files;
    std::vector<std::string> scenery_extenstions = {"Global Scenery", "Custom Scenery"};
    std::vector<std::string> exclusion_patterns = {
        "z_", "ortho", "zortho4xp_", "simheaven_", "x-plane landmarks", "uhd_", "hd_", "library"
    };
    bool in_global_scenery = false;

    if (!fs::exists(xplane_root_path) || !fs::is_directory(xplane_root_path)) {
        std::cerr << "Error: Provided path is not a valid directory: " << xplane_root_path << std::endl;
        return all_apt_files;
    }

    for (const auto& scenery_folder : scenery_extenstions) {
        fs::path scenery_dir = xplane_root_path / scenery_folder;
        if (scenery_folder == "Global Scenery") {
            scenery_dir /= "Global Airports";
            scenery_dir /= "Earth nav data";
            in_global_scenery = true;
        } else {
            in_global_scenery = false;
        }

        std::cout << "Scanning " << scenery_dir.string() << "..." << std::endl; 
        
        // Create a recursive iterator to walk the directory tree
        auto iterator = fs::recursive_directory_iterator(scenery_dir);
        for (const auto& entry : iterator) {
            // Check if the entry is a directory or not (only if in /Custom Scenery)
            if (entry.is_directory() && !in_global_scenery) {
                std::string dir_name = entry.path().filename().string();

                // Convert directory name into lowercase and check against exclusions
                std::transform(dir_name.begin(), dir_name.end(), dir_name.begin(), [](unsigned char c){ return std::tolower(c); });

                for (const auto& pattern : exclusion_patterns) {
                    if (dir_name.find(pattern) != std::string::npos) {
                        iterator.disable_recursion_pending();
                        break;
                    }
                }        
            }

            // If the entry was not skipped, check if it's our target
            if (entry.is_regular_file() && entry.path().filename() == "apt.dat") {
                all_apt_files.push_back(entry.path());
                std::cout << "  -> Located File: " << entry.path().string() << std::endl;
            }
        }
    }
    

    return all_apt_files;
}

void NavDataManager::process_airport_batch(LookaheadLineReader& reader) {
    // Process airport metadata lines into a json to be passed to insert_airport_data()
    json airport_data;
    std::string icao_code, airport_name, dummy_str, current_line;
    int row_code = 0;
    std::vector<std::string> parts;
    bool end_of_block = false;

    // Initialize all potentially missing fields to null. If we find them in the data
    // we will overwrite the null. If not, they remain null for the database.
    airport_data["iata_code"] = nullptr;
    airport_data["faa_code"] = nullptr;
    airport_data["country"] = nullptr;
    airport_data["city"] = nullptr;
    airport_data["region_code"] = nullptr;
    airport_data["transition_alt"] = nullptr;
    airport_data["transition_level"] = nullptr;
    airport_data["datum_lat"] = nullptr;
    airport_data["datum_lon"] = nullptr;

    while (reader.get_next_line(current_line)) {
        if (end_of_block) {
            reader.put_line_back(current_line);
            break;
        }
        row_code = reader.get_row_code(current_line);
        split_string(current_line, parts);

        if (parts.empty()) continue;

        switch(row_code) {
            case 1:
            {
                if (parts.size() >= 5) {
                    airport_data["elevation"] = std::stoi(parts[1]);
                    airport_data["icao_code"] = parts[4];
                    std::string name;
                    for (size_t i = 5; i < parts.size(); ++i) {
                        name += (i > 5 ? " " : "") + parts[i];
                    }
                    airport_data["airport_name"] = name;
                    airport_data["type"] = "Airport";
                }
                break;
            }
            case 16:
            case 17:
            {
                if (parts.size() >= 6) {
                    airport_data["elevation"] = std::stoi(parts[1]);
                    std::string type_designator, temp, name;
                    type_designator = parts[4];
                    icao_code = parts[5];
                    if (icao_code.length() < 4) {
                        temp = icao_code;
                        icao_code = type_designator;
                        type_designator = temp;
                    }
                    airport_data["icao_code"] = icao_code;
                    for (size_t i = 6; i < parts.size(); ++i) {
                        name += (i > 6 ? " " : "") + parts[i];
                    }
                    airport_data["airport_name"] = name;
                    airport_data["type"] = (row_code == 16 ? "Seaplane" : "Heliport");
                }
                break;
            }
            case 1302:
            {
                if (parts.size() >= 3) {
                    std::string key = parts[1];
                    std::string value = parts[2];
                    if (airport_data.contains(key)) {
                        // We need to explicitly convert the numeric types
                        if (key == "transition_alt" || key == "transition_level") {
                            try { airport_data[key] = std::stoi(value); } catch (...) {}
                        } else if (key == "datum_lat" || key == "datum_lon") {
                            try { airport_data[key] = std::stod(value); } catch (...) {}
                        } else {
                            airport_data[key] = value;
                        }
                    }
                }
                break;
            }
            default:
            {
                reader.put_line_back(current_line);
                end_of_block = true;
                break;
            }
        }
    }

    // Send collected data to insertion/update method
    if (!airport_data.is_null() && !airport_data.empty() && airport_data.contains("icao_code")) {
        // Debugging dump
        //std::cout << airport_data.dump(2) << std::endl;
        
        insert_airport_data(airport_data);
    }
}

void NavDataManager::insert_airport_data(const json& airport_data) {
    // Define the SQL statement with names parameters (e.g. :icao) for clarity
    // INSERT OR REPLACE will update a row if the PRIMARY KEY (icao) already exists
    const std::string sql = R"sql(
        INSERT OR REPLACE INTO airports (icao, iata, faa, airport_name, elevation, type, latitude, longitude, country, city, region, transition_alt, transition_level)
        VALUES (:icao, :iata, :faa, :airport_name, :elevation, :type, :latitude, :longitude, :country, :city, :region, :transition_alt, :transition_level);
    )sql";
    
    try {
        SQLite::Statement query(db, sql);

        // Bind values from the json object to the named parameters
        // .value() fields are ones which should always exist, providing a default value
        query.bind(":icao",             airport_data.value("icao_code", "MISSING"));
        query.bind(":airport_name",     airport_data.value("airport_name", "MISSING"));
        query.bind(":elevation",        airport_data.value("elevation", -1));
        query.bind(":type",             airport_data.value("type", "UNKNOWN"));

        // For optional fields, we need to check if they're null before binding.
        if (airport_data["iata_code"].is_null()) {
            query.bind(":iata", SQLITE_NULL);
        } else {
            query.bind(":iata", airport_data["iata_code"].get<std::string>());
        }

        if (airport_data["faa_code"].is_null()) {
            query.bind(":faa", SQLITE_NULL);
        } else {
            query.bind(":faa", airport_data["faa_code"].get<std::string>());
        }

        if (airport_data["country"].is_null()) {
            query.bind(":country", SQLITE_NULL);
        } else {
            query.bind(":country", airport_data["country"].get<std::string>());
        }

        if (airport_data["city"].is_null()) {
            query.bind(":city", SQLITE_NULL);
        } else {
            query.bind(":city", airport_data["city"].get<std::string>());
        }

        if (airport_data["region_code"].is_null()) {
            query.bind(":region", SQLITE_NULL);
        } else {
            query.bind(":region", airport_data["region_code"].get<std::string>());
        }

        if (airport_data["datum_lat"].is_null()) {
            query.bind(":latitude", SQLITE_NULL);
        } else {
            query.bind(":latitude", airport_data["datum_lat"].get<double>());
        }

        if (airport_data["datum_lon"].is_null()) {
            query.bind(":longitude", SQLITE_NULL);
        } else {
            query.bind(":longitude", airport_data["datum_lon"].get<double>());
        }

        if (airport_data["transition_alt"].is_null()) {
            query.bind(":transition_alt", SQLITE_NULL);
        } else {
            query.bind(":transition_alt", airport_data["transition_alt"].get<int>());
        }

        if (airport_data["transition_level"].is_null()) {
            query.bind(":transition_level", SQLITE_NULL);
        } else {
            query.bind(":transition_level", airport_data["transition_level"].get<int>());
        }

        // Execute the query
        query.exec();
    } catch (const std::exception& e) {
        std::cerr << "SQLite error inserting data for ICAO " << airport_data.value("icao_code", "[UNKNOWN]") << ": " << e.what() << std::endl;
    }
    
    // Set current_icao_ for parsing context
    current_icao_ = airport_data.value("icao_code", "MISSING_ICAO");
}

std::optional<json> NavDataManager::get_airport(const std::string& icao) {
    try {
        // Prepare SQL query
        SQLite::Statement query(db, "SELECT * FROM airports WHERE icao = ?");

        query.bind(1, icao);

        // executeStep() reutrns true if a row was fetched
        if (query.executeStep()) {
            json airport_data;

            // Build the json object from the query result
            for (int i = 0; i < query.getColumnCount(); ++i) {
                const SQLite::Column col = query.getColumn(i);
                const std::string col_name = col.getName();

                // Check the type of column and add it to the json object
                switch (col.getType()) {
                    case SQLITE_INTEGER:
                        airport_data[col_name] = col.getInt();
                        break;
                    case SQLITE_FLOAT:
                        airport_data[col_name] = col.getDouble();
                        break;
                    case SQLITE_TEXT:
                        airport_data[col_name] = col.getString();
                        break;
                    case SQLITE_NULL:
                        airport_data[col_name] = nullptr;
                        break;
                    default:
                        airport_data[col_name] = nullptr;
                        break;
                }
            }
            return airport_data;
        }
    } catch (const std::exception& e) {
        std::cerr << "SQLite error fetching airport " << icao << ": " << e.what() << std::endl;
    }

    // If no row was found or an error occured, return an empty optional
    return std::nullopt;
}

void NavDataManager::process_runway_batch(LookaheadLineReader& reader) {
    // Process airport metadata lines into a json to be passed to insert_airport_data()
    json runway_data;
    std::vector<json> runway_jsons;
    std::string dummy_str, current_line;
    int row_code = 0;
    std::vector<std::string> parts;
    bool end_of_block = false;

    while (reader.get_next_line(current_line)) {
        if (end_of_block) {
            reader.put_line_back(current_line);
            break;
        }
        row_code = reader.get_row_code(current_line);
        split_string(current_line, parts);

        if (parts.empty()) continue;

        switch (row_code) {
            case 100:
            {
                if (parts.size() == 26) {
                    try{
                        runway_data["airport_icao"] = current_icao_;
                        runway_data["width"] = std::stof(parts[1]);
                        runway_data["surface"] = std::stoi(parts[2]);
                        runway_data["end1_rw_number"] = parts[8];
                        runway_data["end1_lat"] = std::stod(parts[9]);
                        runway_data["end1_lon"] = std::stod(parts[10]);
                        runway_data["end1_d_threshold"] = std::stof(parts[11]);
                        runway_data["end1_rw_marking_code"] = std::stoi(parts[13]);
                        runway_data["end1_rw_app_light_code"] = std::stoi(parts[14]);
                        runway_data["end2_rw_number"] = parts[17];
                        runway_data["end2_lat"] = std::stod(parts[18]);
                        runway_data["end2_lon"] = std::stod(parts[19]);
                        runway_data["end2_d_threshold"] = std::stof(parts[20]);
                        runway_data["end2_rw_marking_code"] = std::stoi(parts[22]);
                        runway_data["end2_rw_app_light_code"] = std::stoi(parts[23]);

                    } catch (std::exception& e) {
                        std::cerr << "Error parsing runway data for airport " << current_icao_ << ": " << e.what() << std::endl;
                    }
                }
                runway_jsons.push_back(runway_data);
                break;
            }
            default:
            {
                reader.put_line_back(current_line);
                end_of_block = true;
                break;
            }
        }
    }

    if (!runway_data.is_null() && !runway_data.empty() && runway_data.contains("airport_icao")) {
        //std::cout << runway_data.dump(2) << std::endl;

        insert_runway_data(runway_jsons);
    }
}

void NavDataManager::insert_runway_data(std::vector<json>& runway_jsons) {
    const std::string sql = R"sql(
    INSERT INTO runways (airport_icao, width, surface, end1_rw_number, end1_lat, end1_lon, end1_d_threshold, end1_rw_marking_code, end1_rw_app_light_code, end2_rw_number, end2_lat, end2_lon, end2_d_threshold, end2_rw_marking_code, end2_rw_app_light_code)
    VALUES (:airport_icao, :width, :surface, :end1_rw_number, :end1_lat, :end1_lon, :end1_d_threshold, :end1_rw_marking_code, :end1_rw_app_light_code, :end2_rw_number, :end2_lat, :end2_lon, :end2_d_threshold, :end2_rw_marking_code, :end2_rw_app_light_code)
    
    ON CONFLICT(airport_icao, end1_rw_number, end2_rw_number) DO UPDATE SET
        width = excluded.width,
        surface = excluded.surface,
        end1_lat = excluded.end1_lat,
        end1_lon = excluded.end1_lon,
        end1_d_threshold = excluded.end1_d_threshold,
        end1_rw_marking_code = excluded.end1_rw_marking_code,
        end1_rw_app_light_code = excluded.end1_rw_app_light_code,
        end2_lat = excluded.end2_lat,
        end2_lon = excluded.end2_lon,
        end2_d_threshold = excluded.end2_d_threshold,
        end2_rw_marking_code = excluded.end2_rw_marking_code,
        end2_rw_app_light_code = excluded.end2_rw_app_light_code;
    )sql";

    for (const auto& runway_data : runway_jsons) {
        try {
            SQLite::Statement query(db, sql);

            query.bind(":airport_icao",             current_icao_);
            query.bind(":width",                    runway_data.value("width", 0.0));
            query.bind(":surface",                  runway_data.value("surface", 0));
            query.bind(":end1_rw_number",           runway_data.value("end1_rw_number", ""));
            query.bind(":end1_lat",                 runway_data.value("end1_lat", 0.0));
            query.bind(":end1_lon",                 runway_data.value("end1_lon", 0.0));
            query.bind(":end1_d_threshold",         runway_data.value("end1_d_threshold", 0.0));
            query.bind(":end1_rw_marking_code",     runway_data.value("end1_rw_marking_code", 0));
            query.bind(":end1_rw_app_light_code",   runway_data.value("end1_rw_app_light_code", 0));
            query.bind(":end2_rw_number",           runway_data.value("end2_rw_number", ""));
            query.bind(":end2_lat",                 runway_data.value("end2_lat", 0.0));
            query.bind(":end2_lon",                 runway_data.value("end2_lon", 0.0));
            query.bind(":end2_d_threshold",         runway_data.value("end2_d_threshold", 0.0));
            query.bind(":end2_rw_marking_code",     runway_data.value("end2_rw_marking_code", 0));
            query.bind(":end2_rw_app_light_code",   runway_data.value("end2_rw_app_light_code", 0));

            query.exec();
        } catch (const std::exception& e) {
            std::cerr << "SQLite error inserting data for ICAO " << runway_data.value("airport_icao", "[UNKNOWN]") << ": " << e.what() << std::endl;
        }
    }
}

std::vector<json> NavDataManager::get_runways(const std::string& icao) {
    std::vector<json> results;

    try {
        SQLite::Statement query(db, "SELECT * FROM runways WHERE airport_icao = ?");
        query.bind(1, icao);

        // A while loop with executeStep() will iterate over all results.
        while (query.executeStep()) {
            json runway_data;

            // For each row, build the runway_data
            for (int i = 0; i < query.getColumnCount(); ++i) {
                const SQLite::Column col = query.getColumn(i);
                const std::string col_name = col.getName();

                switch (col.getType()) {
                    case SQLITE_INTEGER:
                        runway_data[col_name] = col.getInt64();
                        break;
                    case SQLITE_FLOAT:
                        runway_data[col_name] = col.getDouble();
                        break;
                    case SQLITE_TEXT:
                        runway_data[col_name] = col.getString();
                        break;
                    case SQLITE_NULL:
                        runway_data[col_name] = nullptr;
                        break;
                    default:
                        runway_data[col_name] = nullptr;
                        break;
                }
            }
            results.push_back(runway_data);
        }
    } catch (const std::exception& e) {
        std::cerr << "SQLite error fetching runways for " << icao << ": " << e.what() << std::endl;
    }

    return results;
}

void NavDataManager::update_database(const std::string& xplane_root_path) {
    std::cout << "Scanning for apt.dat files in: " << xplane_root_path << std::endl;

    // Get the list of files
    std::vector<fs::path> apt_files = find_all_apt_dat_files(xplane_root_path);
    int total_file_num = apt_files.size();
    std::cout << "Found " << total_file_num << " apt.dat file(s) to process." << std::endl;

    try {
        SQLite::Transaction transaction(db);
        int cur_file_num = 0;
        // Next is to loop through the files and process them
        for (const fs::path& apt_path : apt_files) {
            LookaheadLineReader reader(apt_path);
            ++cur_file_num;
            std::string line;

            // Display message to console
            std::cout << "\n(" << cur_file_num << "/" << total_file_num << ") Processing File: " << apt_path.string() << std::endl;

            auto last_update_time = std::chrono::steady_clock::now();
            const auto update_interval = std::chrono::milliseconds(50);

            while (reader.get_next_line(line)) {
                // Check if it's time to update the progress bar
                auto current_time = std::chrono::steady_clock::now();
                if (current_time - last_update_time > update_interval) {
                    reader.display_progress();
                    last_update_time = current_time;
                }
                // We found a blank line, assume current airport has been parsed,
                // so reset current_icao_ context and continue to next iteration.
                if (line.empty()) {
                    current_icao_ = "";
                    continue;
                }
                
                // Get the row_code for the current line
                int row_code = reader.get_row_code(line);

                // If the row_code is not an integer, continue on to the next loop iteration
                if (row_code == -1) continue;

                // --- Dispatcher ---
                // Look at the row_code, and determine what kind of block-processing we want to do
                
                // AIRPORT METADATA
                if (row_code == 1 || row_code == 16 || row_code == 17 || row_code == 1302) {
                    reader.put_line_back(line);
                    process_airport_batch(reader);
                }

                // RUNWAY DATA
                else if (row_code == 100) {
                    reader.put_line_back(line);
                    process_runway_batch(reader);
                }
            }
            reader.finialize_progress();
        }

        std::cout << "Committing database changes..." << std::endl;
        transaction.commit();
        std::cout << "Database update complete." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "A critical error occurred during the database update transaction: " << e.what() << std::endl;
    }
}

void NavDataManager::split_string(const std::string& line, std::vector<std::string>& parts) {
    parts.clear();
    std::string current_part;
    current_part.reserve(32);

    for (size_t i = 0; i < line.length(); ) {
        int char_len = 1; // Default to 1 for ASCII and invalid bytes
        unsigned char lead = line[i];

        if (lead >= 0xC0 && lead <= 0xDF) {      // 2-byte character
            char_len = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) { // 3-byte character
            char_len = 3;
        } else if (lead >= 0xF0 && lead <= 0xF7) { // 4-byte character
            char_len = 4;
        }
        // Note: This is a simplified check that doesn't fully validate but is sufficient for splitting.

        // Ensure we don't read past the end of the string
        if (i + char_len > line.length()) {
            char_len = line.length() - i;
        }

        // The apt.dat format uses simple ASCII whitespace as separators.
        // We only need to check if the character is a space or tab.
        // Since these are single-byte ASCII, we only need to check the lead byte.
        if (lead == ' ' || lead == '\t') {
            if (!current_part.empty()) {
                parts.push_back(current_part);
                current_part.clear();
            }
        } else {
            // Append the entire multi-byte character as a single unit
            current_part.append(line, i, char_len);
        }

        // Advance the index by the full length of the character
        i += char_len;
    }

    if (!current_part.empty()) {
        parts.push_back(current_part);
    }
}