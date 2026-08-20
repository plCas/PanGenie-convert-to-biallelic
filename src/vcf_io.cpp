#include "convert_to_biallelic/vcf_io.hpp"

#include <htslib/bgzf.h>
#include <htslib/hfile.h>
#include <htslib/hts.h>
#include <htslib/kseq.h>
#include <htslib/kstring.h>

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

int close_descriptor(int descriptor) noexcept {
#ifdef _WIN32
    return ::_close(descriptor);
#else
    return ::close(descriptor);
#endif
}

std::string errno_message(int error) {
    return std::generic_category().message(error);
}

class DescriptorGuard {
public:
    explicit DescriptorGuard(int descriptor) noexcept
        : descriptor_(descriptor) {}

    ~DescriptorGuard() noexcept {
        if (descriptor_ >= 0) {
            (void)close_descriptor(descriptor_);
        }
    }

    DescriptorGuard(const DescriptorGuard&) = delete;
    DescriptorGuard& operator=(const DescriptorGuard&) = delete;

    int release() noexcept {
        const int descriptor = descriptor_;
        descriptor_ = -1;
        return descriptor;
    }

    int get() const noexcept { return descriptor_; }

private:
    int descriptor_;
};

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

void InputSource::close() {
    if (!impl_) {
        throw state_error("close", "moved-from");
    }
    if (impl_->file == nullptr) {
        return;
    }

    htsFile* const handle = impl_->file;
    impl_->file = nullptr;
    if (hts_close(handle) != 0) {
        throw std::runtime_error("Failed to close input VCF '" +
                                 impl_->path_text + "'");
    }
}

struct OutputSink::Impl {
    Impl(const std::filesystem::path& path,
         OutputFormat format,
         std::size_t io_workers)
        : pending_descriptor(-1), path_text(path_to_utf8(path)) {
        hFILE* stream = hopen(path_text.c_str(), "wb");
        if (stream == nullptr) {
            throw std::runtime_error("Failed to open VCF output '" +
                                     path_text + "'");
        }
        adopt_hfile(stream, format, io_workers);
    }

    Impl(int owned_fd,
         const std::filesystem::path& display_path,
         OutputFormat format,
         std::size_t io_workers)
        : pending_descriptor(owned_fd), path_text(path_to_utf8(display_path)) {
        if (owned_fd < 0) {
            throw std::invalid_argument(
                "Invalid owned output descriptor for '" + path_text + "'");
        }

        hFILE* stream = hdopen(pending_descriptor.get(), "wb");
        if (stream == nullptr) {
            const int adoption_error = errno;
            const int descriptor = pending_descriptor.release();
            if (close_descriptor(descriptor) != 0) {
                const int close_error = errno;
                throw std::runtime_error(
                    "Failed to adopt owned output descriptor for '" +
                    path_text + "': " + errno_message(adoption_error) +
                    "; descriptor cleanup also failed: " +
                    errno_message(close_error));
            }
            throw std::runtime_error(
                "Failed to adopt owned output descriptor for '" + path_text +
                "': " + errno_message(adoption_error));
        }
        (void)pending_descriptor.release();
        adopt_hfile(stream, format, io_workers);
    }

    void adopt_hfile(hFILE* stream,
                     OutputFormat format,
                     std::size_t io_workers) {
        try {
            switch (format) {
                case OutputFormat::vcf:
                    plain = stream;
                    break;

                case OutputFormat::vcf_gz:
                    compressed = bgzf_hopen(stream, "w");
                    if (compressed == nullptr) {
                        const int error = errno;
                        hclose_abruptly(stream);
                        throw std::runtime_error(
                            "Failed to wrap owned hFILE as BGZF VCF output '" +
                            path_text + "': " + errno_message(error));
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
                    hclose_abruptly(stream);
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

    // Declared first so descriptor ownership survives exceptions from display
    // path allocation and every later member/constructor operation.
    DescriptorGuard pending_descriptor;
    std::string path_text;
    hFILE* plain = nullptr;
    BGZF* compressed = nullptr;
};

OutputSink::OutputSink(const std::filesystem::path& path,
                       OutputFormat format,
                       std::size_t io_workers)
    : impl_(std::make_unique<Impl>(path, format, io_workers)) {}

OutputSink::OutputSink(int owned_fd,
                       const std::filesystem::path& display_path,
                       OutputFormat format,
                       std::size_t io_workers) {
    DescriptorGuard descriptor(owned_fd);
    // A new-expression allocates before evaluating its initializer. If the
    // allocation fails, the guard still owns the descriptor; once release()
    // runs, Impl is solely responsible for every constructor path.
    impl_.reset(new Impl(descriptor.release(), display_path, format,
                         io_workers));
}

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
