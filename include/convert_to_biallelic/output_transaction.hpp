#pragma once

#include <filesystem>
#include <memory>

namespace ctb {

class OutputTransaction {
public:
    OutputTransaction(std::filesystem::path destination, bool force);
    ~OutputTransaction() noexcept;

    OutputTransaction(const OutputTransaction&) = delete;
    OutputTransaction& operator=(const OutputTransaction&) = delete;
    OutputTransaction(OutputTransaction&&) = delete;
    OutputTransaction& operator=(OutputTransaction&&) = delete;

    const std::filesystem::path& temporary_path() const noexcept;
    int take_sink_fd();
    void commit();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ctb
