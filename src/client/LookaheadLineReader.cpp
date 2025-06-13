#include "LookaheadLineReader.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <stdexcept>

LookaheadLineReader::LookaheadLineReader(const fs::path& file_path) : file_stream_(file_path), buffered_line_(), total_size_(0), bytes_processed_(0) {
    if (!file_stream_.is_open()) {
        throw std::runtime_error("Could not open file: " + file_path.string());
    }
    if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
        total_size_ = fs::file_size(file_path);
    }
}

bool LookaheadLineReader::get_next_line(std::string& out_line) {
    bool success = false;
    if (buffered_line_) {
        out_line = std::move(*buffered_line_);
        buffered_line_.reset();
        success = true;
    } else {
        success = static_cast<bool>(std::getline(file_stream_, out_line));
    }

    if (success) {
        bytes_processed_ += out_line.length() + 1;
    }
    return success;
}

void LookaheadLineReader::put_line_back(const std::string& line) {
    if (buffered_line_) {
        throw std::logic_error("Cannot put back more than one line at a time.");
    }
    buffered_line_ = line;
    // Remove the processed value from the running total to maintain consistency
    bytes_processed_ -= line.length() + 1;
}

int LookaheadLineReader::get_row_code(const std::string& line) {
    int row_code = -1;
    std::string s_row_code;
    if (std::isdigit(line[0])) {
        for (char ch : line) {
            if (isspace(ch)) {
                if (!s_row_code.empty()) {
                    row_code = std::stoi(s_row_code);
                    break;
                }
            } else {
                s_row_code += ch;
            }
        }
    }
    return row_code;
}

void LookaheadLineReader::display_progress() const {
    if (total_size_ == 0) return;
    float percentage = static_cast<float>(bytes_processed_) / total_size_;
    if (percentage > 1.0f) percentage = 1.0f;

    int bar_width = 50;
    int pos = static_cast<int>(bar_width * percentage);

    // --- Build the entire output in an in-memory stringstream ---
    std::stringstream ss;
    ss << "\r[";

    // Build the bar part
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) {
            ss << "=";
        } else if (i == pos) {
            ss << ">";
        } else {
            ss << " ";
        }
    }

    // Add the text part
    ss << "] " << std::setw(3) << static_cast<int>(percentage * 100.0) << "%"
       << std::setw(11) << bytes_processed_ << "/" << total_size_ << " bytes";

    // --- Perform a single write to the console and flush ---
    std::cout << ss.str() << std::flush;
}

void LookaheadLineReader::finialize_progress() const {
    std::cout << "\r[";
    for (int i = 0; i < 50; ++i) {
        std::cout << "=";
    }
    std::cout << "] 100%" << std::setw(11) << bytes_processed_ << "/" << total_size_ << " bytes" << std::endl;
}