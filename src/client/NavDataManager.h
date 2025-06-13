#pragma once
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include "nlohmann/json.hpp"
#include "LookaheadLineReader.h"
#include <SQLiteCpp/Database.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

class NavDataManager {
    public:
        // Constructor opens the database file
        NavDataManager(const std::string& db_path);

        // Parse apt.dat files and populate the database
        void update_database(const std::string& xplane_root_path);

        // Fetches a single airport by its ICAO code
        std::optional<json> get_airport(const std::string& icao);

    private:
        // The database connection object
        SQLite::Database db;

        // Helper methods
        void create_tables();
        void split_string(const std::string& line, std::vector<std::string>& parts);
        std::vector<fs::path> find_all_apt_dat_files(const fs::path& xplane_root_path);
        void process_airport_batch(LookaheadLineReader& reader);
        void insert_airport_data(const json& airport_data);
};