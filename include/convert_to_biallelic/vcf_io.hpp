#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "convert_to_biallelic/types.hpp"

namespace ctb {

class InputSource {
public:
    InputSource(const std::filesystem::path& path, std::size_t io_workers);
    ~InputSource();

    InputSource(InputSource&&) noexcept;
    InputSource& operator=(InputSource&&) noexcept;

    bool getline(std::string& line);
    bool compressed() const noexcept;
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class OutputSink {
public:
    OutputSink(const std::filesystem::path& path,
               OutputFormat format,
               std::size_t io_workers);
    OutputSink(int owned_fd,
               const std::filesystem::path& display_path,
               OutputFormat format,
               std::size_t io_workers);
    ~OutputSink();

    OutputSink(OutputSink&&) noexcept;
    OutputSink& operator=(OutputSink&&) noexcept;

    void write(std::string_view bytes);
    void flush();
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ctb
