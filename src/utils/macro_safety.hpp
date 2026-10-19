#pragma once

#include <filesystem>
#include <fstream>
#include <system_error>
#include <chrono>
#include <string>
#include <cstdint>
#include <cstddef>

namespace xdbot::safety {
    // Reject empty, unreadable, oversized, or implausibly named macro files before allocation.
    inline bool validateMacroFile(std::filesystem::path const& path, std::string& reason,
                                  std::uintmax_t maxBytes = 256ull * 1024ull * 1024ull) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) {
            reason = "The selected path is not a readable regular file.";
            return false;
        }
        auto size = std::filesystem::file_size(path, ec);
        if (ec) {
            reason = "Could not determine the file size.";
            return false;
        }
        if (size == 0) {
            reason = "The selected macro file is empty.";
            return false;
        }
        if (size > maxBytes) {
            reason = "The selected file exceeds the 256 MiB safety limit.";
            return false;
        }
        auto ext = path.extension().string();
        if (ext != ".gdr" && ext != ".json" && ext != ".xd") {
            reason = "Unsupported macro extension.";
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input.good()) {
            reason = "The selected file could not be opened for reading.";
            return false;
        }
        return true;
    }

    // Preserve the original in a timestamped sibling before destructive operations.
    inline bool backupFile(std::filesystem::path const& source, std::string& reason) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(source, ec) || ec) {
            reason = "The original file does not exist or is not a regular file.";
            return false;
        }
        auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto destination = source;
        destination += ".backup-" + std::to_string(stamp);
        std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec);
        if (ec) {
            reason = "Could not create backup: " + ec.message();
            return false;
        }
        return true;
    }

    // Write to a sibling temporary file and rename only after the stream is flushed.
    inline bool atomicWrite(std::filesystem::path const& destination,
                            std::uint8_t const* data, std::size_t size,
                            std::string& reason) {
        auto temporary = destination;
        temporary += ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out) { reason = "Could not open temporary output file."; return false; }
            out.write(reinterpret_cast<char const*>(data), static_cast<std::streamsize>(size));
            out.flush();
            if (!out) {
                reason = "Writing the temporary file failed.";
                out.close();
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return false;
            }
        }
        std::error_code ec;
        // Windows does not replace an existing file with rename; callers use unique import names.
        std::filesystem::rename(temporary, destination, ec);
        if (ec) {
            reason = "Could not finalize output file: " + ec.message();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        return true;
    }
}
