# Task 3 Complete Review Package

## Review scope and constraints

This is a static, full-file review package for Task 3. No installation,
configuration, compilation, execution, tests, benchmarks, Git operations, or
commits were performed. Python was not changed. All HTSlib API/build/runtime
behavior and Windows Unicode-path portability remain unverified.

Behavioral edge-case resolution supplied by the task owner:

- `InputSource::compressed() const noexcept` returns `false` on a moved-from
  object because its required signature cannot throw.
- `OutputSink::close()` is a no-op after an actual close.
- `getline`, `write`, and `flush` throw descriptive exceptions when their object
  is moved-from or closed. `close()` throws on moved-from state.

## Static review checklist

- [x] Required public declarations are in namespace `ctb`.
- [x] Required public HTSlib headers are included.
- [x] Input opens with `hts_open(path, "r")`.
- [x] Only detected text VCF with plain, gzip, or BGZF compression is accepted.
- [x] Compressed input conditionally calls `hts_set_threads`.
- [x] `hts_getline` EOF, error, and byte-copy rules are represented.
- [x] `compressed()` reflects gzip/BGZF detection.
- [x] Plain output uses hFILE open/write/flush/close operations.
- [x] Compressed output uses BGZF open/MT/write/flush/close operations.
- [x] Output format comes only from explicit `OutputFormat`.
- [x] Output close is idempotent after close and reports flush/close failures.
- [x] Destructors perform best-effort, non-throwing cleanup.
- [x] Move operations transfer unique ownership.
- [x] Constructor failure paths release handles acquired before the failure.
- [x] Invalid operational states are handled descriptively within signature
      constraints.
- [x] Filesystem-to-UTF-8 conversion is isolated.
- [x] HTSlib and Windows Unicode behavior is labeled unverified.

## Exact full contents: `include/convert_to_biallelic/vcf_io.hpp`

```cpp
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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class OutputSink {
public:
    OutputSink(const std::filesystem::path& path,
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
```

## Exact full contents: `src/vcf_io.cpp`

```cpp
#include "convert_to_biallelic/vcf_io.hpp"

#include <htslib/bgzf.h>
#include <htslib/hfile.h>
#include <htslib/hts.h>
#include <htslib/kstring.h>

#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

namespace ctb {
namespace {

std::string path_to_utf8(const std::filesystem::path& path) {
    return path.u8string();
}

int checked_thread_count(std::size_t io_workers,
                         const std::string& path_text) {
    if (io_workers > static_cast<std::size_t>(
                         std::numeric_limits<int>::max())) {
        throw std::runtime_error("I/O worker count is too large for HTSlib for '" +
                                 path_text + "'");
    }
    return static_cast<int>(io_workers);
}

std::runtime_error state_error(const char* operation, const char* state) {
    return std::runtime_error(std::string("Cannot ") + operation + " a " + state +
                              " VCF I/O object");
}

}  // namespace

struct InputSource::Impl {
    explicit Impl(const std::filesystem::path& path, std::size_t io_workers)
        : path_text(path_to_utf8(path)) {
        file = hts_open(path_text.c_str(), "r");
        if (file == nullptr) {
            throw std::runtime_error("Failed to open input VCF '" + path_text +
                                     "'");
        }

        try {
            const htsFormat* detected = hts_get_format(file);
            if (detected == nullptr || detected->format != vcf) {
                throw std::runtime_error("Input '" + path_text +
                                         "' is not a text VCF file");
            }

            switch (detected->compression) {
                case no_compression:
                    is_compressed = false;
                    break;
                case gzip:
                case bgzf:
                    is_compressed = true;
                    break;
                default:
                    throw std::runtime_error(
                        "Input VCF '" + path_text +
                        "' uses unsupported compression; expected plain, gzip, or BGZF");
            }

            if (is_compressed && io_workers > 0) {
                const int thread_count =
                    checked_thread_count(io_workers, path_text);
                if (hts_set_threads(file, thread_count) != 0) {
                    throw std::runtime_error(
                        "Failed to enable threaded input decompression for '" +
                        path_text + "'");
                }
            }
        } catch (...) {
            (void)hts_close(file);
            file = nullptr;
            throw;
        }
    }

    ~Impl() noexcept {
        if (file != nullptr) {
            (void)hts_close(file);
        }
        std::free(line.s);
    }

    std::string path_text;
    htsFile* file = nullptr;
    kstring_t line{0, 0, nullptr};
    bool is_compressed = false;
};

InputSource::InputSource(const std::filesystem::path& path,
                         std::size_t io_workers)
    : impl_(std::make_unique<Impl>(path, io_workers)) {}

InputSource::~InputSource() = default;
InputSource::InputSource(InputSource&&) noexcept = default;
InputSource& InputSource::operator=(InputSource&&) noexcept = default;

bool InputSource::getline(std::string& line) {
    if (!impl_) {
        throw state_error("read from", "moved-from");
    }
    if (impl_->file == nullptr) {
        throw state_error("read from", "closed");
    }

    const int result = hts_getline(impl_->file, KS_SEP_LINE, &impl_->line);
    if (result == -1) {
        return false;
    }
    if (result < -1) {
        throw std::runtime_error("Failed while reading input VCF '" +
                                 impl_->path_text + "'");
    }

    if (impl_->line.l == 0) {
        line.clear();
    } else {
        line.assign(impl_->line.s, impl_->line.l);
    }
    return true;
}

bool InputSource::compressed() const noexcept {
    return impl_ != nullptr && impl_->is_compressed;
}

struct OutputSink::Impl {
    Impl(const std::filesystem::path& path,
         OutputFormat format,
         std::size_t io_workers)
        : path_text(path_to_utf8(path)) {
        try {
            switch (format) {
                case OutputFormat::vcf:
                    plain = hopen(path_text.c_str(), "wb");
                    if (plain == nullptr) {
                        throw std::runtime_error(
                            "Failed to open plain VCF output '" + path_text + "'");
                    }
                    break;

                case OutputFormat::vcf_gz:
                    compressed = bgzf_open(path_text.c_str(), "w");
                    if (compressed == nullptr) {
                        throw std::runtime_error(
                            "Failed to open BGZF VCF output '" + path_text + "'");
                    }
                    if (io_workers > 0) {
                        const int thread_count =
                            checked_thread_count(io_workers, path_text);
                        constexpr int kThreadQueueBlocks = 256;
                        if (bgzf_mt(compressed, thread_count, kThreadQueueBlocks) !=
                            0) {
                            throw std::runtime_error(
                                "Failed to enable threaded BGZF output for '" +
                                path_text + "'");
                        }
                    }
                    break;

                default:
                    throw std::runtime_error("Unsupported output format for '" +
                                             path_text + "'");
            }
        } catch (...) {
            if (plain != nullptr) {
                (void)hclose(plain);
                plain = nullptr;
            }
            if (compressed != nullptr) {
                (void)bgzf_close(compressed);
                compressed = nullptr;
            }
            throw;
        }
    }

    ~Impl() noexcept {
        if (plain != nullptr) {
            (void)hflush(plain);
            (void)hclose(plain);
        }
        if (compressed != nullptr) {
            (void)bgzf_flush(compressed);
            (void)bgzf_close(compressed);
        }
    }

    bool is_closed() const noexcept {
        return plain == nullptr && compressed == nullptr;
    }

    void write(std::string_view bytes) {
        if (is_closed()) {
            throw state_error("write to", "closed");
        }

        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const std::size_t remaining = bytes.size() - offset;
            const auto written = plain != nullptr
                                     ? hwrite(plain, bytes.data() + offset, remaining)
                                     : bgzf_write(compressed,
                                                  bytes.data() + offset,
                                                  remaining);
            if (written <= 0) {
                throw std::runtime_error("Failed while writing output VCF '" +
                                         path_text + "'");
            }
            offset += static_cast<std::size_t>(written);
        }
    }

    void flush() {
        if (is_closed()) {
            throw state_error("flush", "closed");
        }

        const int status = plain != nullptr ? hflush(plain)
                                            : bgzf_flush(compressed);
        if (status != 0) {
            throw std::runtime_error("Failed to flush output VCF '" + path_text +
                                     "'");
        }
    }

    void close() {
        if (is_closed()) {
            return;
        }

        int flush_status = 0;
        int close_status = 0;
        if (plain != nullptr) {
            hFILE* handle = plain;
            plain = nullptr;
            flush_status = hflush(handle);
            close_status = hclose(handle);
        } else {
            BGZF* handle = compressed;
            compressed = nullptr;
            flush_status = bgzf_flush(handle);
            close_status = bgzf_close(handle);
        }

        if (flush_status != 0 && close_status != 0) {
            throw std::runtime_error("Failed to flush and close output VCF '" +
                                     path_text + "'");
        }
        if (flush_status != 0) {
            throw std::runtime_error("Failed to flush output VCF while closing '" +
                                     path_text + "'");
        }
        if (close_status != 0) {
            throw std::runtime_error("Failed to close output VCF '" + path_text +
                                     "'");
        }
    }

    std::string path_text;
    hFILE* plain = nullptr;
    BGZF* compressed = nullptr;
};

OutputSink::OutputSink(const std::filesystem::path& path,
                       OutputFormat format,
                       std::size_t io_workers)
    : impl_(std::make_unique<Impl>(path, format, io_workers)) {}

OutputSink::~OutputSink() = default;
OutputSink::OutputSink(OutputSink&&) noexcept = default;
OutputSink& OutputSink::operator=(OutputSink&&) noexcept = default;

void OutputSink::write(std::string_view bytes) {
    if (!impl_) {
        throw state_error("write to", "moved-from");
    }
    impl_->write(bytes);
}

void OutputSink::flush() {
    if (!impl_) {
        throw state_error("flush", "moved-from");
    }
    impl_->flush();
}

void OutputSink::close() {
    if (!impl_) {
        throw state_error("close", "moved-from");
    }
    impl_->close();
}

}  // namespace ctb
```

## Review findings

Static review found no known ownership leak in the represented success,
constructor-failure, explicit-close, destructor, or move paths. Close errors are
propagated only from explicit `OutputSink::close()`; destructors deliberately
discard errors. Format selection is explicit and path suffixes are never used.

The review cannot establish build compatibility or runtime correctness. HTSlib
header availability, public declarations, return-value semantics, file-format
detection, compression threading, short-write behavior, close ownership, and
Windows UTF-8 path acceptance are all source-assumed and unverified. The 256-block
`bgzf_mt` queue depth is also unverified for the eventual environment.
