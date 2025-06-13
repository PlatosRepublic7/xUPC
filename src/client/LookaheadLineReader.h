#pragma once
#include <fstream>
#include <optional>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

class LookaheadLineReader {
    // A class to manage file reading and lookahead during parsing
    public:
        // Constructor opens the file
        explicit LookaheadLineReader(const fs::path& file_path);

        // Tries to get the next line, either from the buffer or from the file.
        // Returns false if there are no more lines.
        bool get_next_line(std::string& out_line);

        // Puts a line back into the buffer to be read next.
        void put_line_back(const std::string& line);

        // Get the line's row_code
        // Returns -1 if the row_code is not an integer
        int get_row_code(const std::string& line);

        // Displays the progress bar based on the internal state.
        void display_progress() const;

        // Prints the final 100% bar and a newline for a clean finish.
        void finialize_progress() const;

    private:
        std::ifstream file_stream_;
        std::optional<std::string> buffered_line_;
        long long total_size_;
        long long bytes_processed_;
};