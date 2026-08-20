#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include "convert_to_biallelic/output_transaction.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#error "OutputTransaction supports only Windows and Linux"
#endif

namespace ctb {
namespace {

std::atomic<std::uint64_t> temporary_counter{0};
constexpr std::uint64_t kMaximumCreationAttempts = 1024ULL * 1024ULL;

std::string display_path(const std::filesystem::path& path) {
    return path.u8string();
}

std::string system_message(unsigned long error) {
#ifdef _WIN32
    return std::system_category().message(static_cast<int>(error));
#elif defined(__linux__)
    return std::generic_category().message(static_cast<int>(error));
#endif
}

std::string errno_message(int error) {
    return std::generic_category().message(error);
}

[[noreturn]] void throw_path_error(
    std::string_view operation,
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    unsigned long error) {
    throw std::runtime_error(
        std::string(operation) + " from '" + display_path(source) +
        "' to '" + display_path(destination) + "': " +
        system_message(error));
}

[[noreturn]] void throw_filesystem_error(
    std::string_view operation,
    const std::filesystem::path& path,
    const std::filesystem::path& destination,
    const std::error_code& error) {
    throw std::runtime_error(std::string(operation) + " '" +
                             display_path(path) + "' for destination '" +
                             display_path(destination) + "': " +
                             error.message());
}

void append_ascii(std::filesystem::path::string_type& destination,
                  std::string_view ascii) {
    for (const char character : ascii) {
        destination.push_back(
            static_cast<std::filesystem::path::value_type>(character));
    }
}

std::uint64_t process_id() noexcept {
#ifdef _WIN32
    return static_cast<std::uint64_t>(::GetCurrentProcessId());
#elif defined(__linux__)
    return static_cast<std::uint64_t>(::getpid());
#endif
}

std::filesystem::path make_candidate_name(
    const std::filesystem::path& filename,
    std::uint64_t counter,
    std::string_view role = {}) {
    std::filesystem::path::string_type name;
    name.push_back(static_cast<std::filesystem::path::value_type>('.'));
    name += filename.native();
    append_ascii(name, ".ctb.");
    if (!role.empty()) {
        append_ascii(name, role);
        name.push_back(static_cast<std::filesystem::path::value_type>('.'));
    }
    append_ascii(name, std::to_string(process_id()));
    name.push_back(static_cast<std::filesystem::path::value_type>('.'));
    append_ascii(name, std::to_string(counter));
    append_ascii(name, ".tmp");
    return std::filesystem::path(std::move(name));
}

std::filesystem::path absolute_destination(
    const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::path absolute =
        std::filesystem::absolute(destination, error);
    if (error) {
        throw_filesystem_error("Failed to resolve output destination",
                               destination, destination, error);
    }
    return absolute.lexically_normal();
}

void validate_parent(const std::filesystem::path& parent,
                     const std::filesystem::path& destination) {
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::status(parent, error);
    if (error) {
        throw_filesystem_error("Failed to inspect output parent directory",
                               parent, destination, error);
    }
    if (!std::filesystem::is_directory(status)) {
        throw std::invalid_argument(
            "Output parent '" + display_path(parent) +
            "' for destination '" + display_path(destination) +
            "' is not an existing directory");
    }
}

#ifdef _WIN32
bool windows_path_entry_exists(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, error);
    if (error) {
        throw_filesystem_error("Failed to inspect output destination", path,
                               path, error);
    }
    if (status.type() == std::filesystem::file_type::not_found) {
        return false;
    }
    if (!std::filesystem::status_known(status)) {
        throw std::runtime_error(
            "Output destination has unknown filesystem status: '" +
            display_path(path) + "'");
    }
    return true;
}

bool same_windows_identity(const BY_HANDLE_FILE_INFORMATION& left,
                           const BY_HANDLE_FILE_INFORMATION& right) noexcept {
    return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
           left.nFileIndexHigh == right.nFileIndexHigh &&
           left.nFileIndexLow == right.nFileIndexLow;
}

void verify_windows_path_identity(
    HANDLE retained,
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) {
    BY_HANDLE_FILE_INFORMATION retained_info{};
    if (::GetFileInformationByHandle(retained, &retained_info) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to inspect retained temporary output identity",
                         temporary, destination, error);
    }

    HANDLE path_handle = ::CreateFileW(
        temporary.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (path_handle == INVALID_HANDLE_VALUE) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to open temporary output path for identity verification",
                         temporary, destination, error);
    }

    BY_HANDLE_FILE_INFORMATION path_info{};
    if (::GetFileInformationByHandle(path_handle, &path_info) == 0) {
        const DWORD inspect_error = ::GetLastError();
        if (::CloseHandle(path_handle) == 0) {
            const DWORD close_error = ::GetLastError();
            throw std::runtime_error(
                "Failed to inspect temporary output path identity '" +
                display_path(temporary) + "' for destination '" +
                display_path(destination) + "': " +
                system_message(inspect_error) +
                "; verification-handle cleanup also failed: " +
                system_message(close_error));
        }
        throw_path_error("Failed to inspect temporary output path identity",
                         temporary, destination, inspect_error);
    }
    if (::CloseHandle(path_handle) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to close temporary identity verification handle",
                         temporary, destination, error);
    }
    if (!same_windows_identity(retained_info, path_info)) {
        throw std::runtime_error(
            "Temporary output path no longer names the retained file identity: '" +
            display_path(temporary) + "' for destination '" +
            display_path(destination) + "'");
    }
}
#endif

#ifdef __linux__
bool tmpfile_is_unsupported(int error) noexcept {
    return error == EOPNOTSUPP || error == EISDIR || error == ENOENT ||
           error == EINVAL;
}

bool same_linux_identity(const struct stat& left,
                         const struct stat& right) noexcept {
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

bool linux_named_identity_matches(
    int retained_fd,
    int directory_fd,
    const std::filesystem::path& temporary_name,
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) {
    struct stat retained_status {};
    if (::fstat(retained_fd, &retained_status) != 0) {
        const int error = errno;
        throw_path_error("Failed to inspect retained Linux output identity",
                         temporary, destination,
                         static_cast<unsigned long>(error));
    }

    struct stat path_status {};
    if (::fstatat(directory_fd, temporary_name.c_str(), &path_status,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        const int error = errno;
        throw_path_error("Failed to inspect linked temporary output identity",
                         temporary, destination,
                         static_cast<unsigned long>(error));
    }
    return same_linux_identity(retained_status, path_status);
}

struct LinkIdentityResult {
    bool linked = false;
    int error = 0;
    int empty_path_error = 0;
    bool procfs_attempted = false;
};

bool should_try_procfs_link(int error) noexcept {
    return error == ENOENT || error == EPERM || error == EACCES ||
           error == EINVAL;
}

LinkIdentityResult link_anonymous_identity(
    int retained_fd,
    int directory_fd,
    const std::filesystem::path& destination_name) {
    if (::linkat(retained_fd, "", directory_fd,
                 destination_name.c_str(), AT_EMPTY_PATH) == 0) {
        return LinkIdentityResult{true, 0, 0, false};
    }

    const int empty_path_error = errno;
    if (!should_try_procfs_link(empty_path_error)) {
        return LinkIdentityResult{false, empty_path_error,
                                  empty_path_error, false};
    }

    const std::string proc_path =
        "/proc/self/fd/" + std::to_string(retained_fd);
    if (::linkat(AT_FDCWD, proc_path.c_str(), directory_fd,
                 destination_name.c_str(), AT_SYMLINK_FOLLOW) == 0) {
        return LinkIdentityResult{true, 0, empty_path_error, true};
    }
    return LinkIdentityResult{false, errno, empty_path_error, true};
}

[[noreturn]] void throw_link_identity_error(
    std::string_view operation,
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const LinkIdentityResult& result) {
    if (result.procfs_attempted) {
        throw std::runtime_error(
            std::string(operation) + " from retained identity '" +
            display_path(source) + "' to '" + display_path(destination) +
            "': AT_EMPTY_PATH failed: " +
            system_message(static_cast<unsigned long>(result.empty_path_error)) +
            "; /proc/self/fd fallback failed: " +
            system_message(static_cast<unsigned long>(result.error)));
    }
    throw_path_error(operation, source, destination,
                     static_cast<unsigned long>(result.error));
}
#endif

}  // namespace

struct OutputTransaction::Impl {
    std::filesystem::path destination;
    std::filesystem::path parent;
    std::filesystem::path destination_name;
    std::filesystem::path temporary;
    bool force = false;
    bool committed = false;
    bool sink_fd_taken = false;

#ifdef _WIN32
    HANDLE file_handle = INVALID_HANDLE_VALUE;
#elif defined(__linux__)
    int file_fd = -1;
    int directory_fd = -1;
    bool anonymous_tmpfile = false;
    std::filesystem::path temporary_name;
    bool temporary_linked = false;
    std::filesystem::path staging_name;
    bool staging_linked = false;
#endif

    ~Impl() noexcept {
#ifdef _WIN32
        if (file_handle == INVALID_HANDLE_VALUE) {
            return;
        }
        if (!committed) {
            FILE_DISPOSITION_INFO disposition{};
            disposition.DeleteFile = TRUE;
            (void)::SetFileInformationByHandle(
                file_handle, FileDispositionInfo, &disposition,
                static_cast<DWORD>(sizeof(disposition)));
        }
        (void)::CloseHandle(file_handle);
#elif defined(__linux__)
        if (directory_fd >= 0) {
            if (temporary_linked && !temporary_name.empty()) {
                (void)::unlinkat(directory_fd,
                                 temporary_name.c_str(), 0);
            }
            if (!committed && staging_linked && !staging_name.empty()) {
                (void)::unlinkat(directory_fd, staging_name.c_str(), 0);
            }
        }
        if (file_fd >= 0) {
            (void)::close(file_fd);
        }
        if (directory_fd >= 0) {
            (void)::close(directory_fd);
        }
#endif
    }
};

OutputTransaction::OutputTransaction(std::filesystem::path destination,
                                     bool force)
    : impl_(std::make_unique<Impl>()) {
    if (destination.filename().empty()) {
        throw std::invalid_argument(
            "Output destination must have a nonempty filename: '" +
            display_path(destination) + "'");
    }

    impl_->destination = absolute_destination(destination);
    impl_->destination_name = impl_->destination.filename();
    impl_->parent = impl_->destination.parent_path();
    impl_->force = force;
    validate_parent(impl_->parent, impl_->destination);

#ifdef _WIN32
    if (!force && windows_path_entry_exists(impl_->destination)) {
        throw std::runtime_error("Output destination already exists: '" +
                                 display_path(impl_->destination) + "'");
    }

    for (std::uint64_t attempt = 0; attempt < kMaximumCreationAttempts;
         ++attempt) {
        const std::uint64_t counter =
            temporary_counter.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path candidate =
            impl_->parent /
            make_candidate_name(impl_->destination_name, counter);

        HANDLE handle = ::CreateFileW(
            candidate.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD error = ::GetLastError();
            if (error == ERROR_FILE_EXISTS) {
                continue;
            }
            throw_path_error("Failed to exclusively create temporary output",
                             candidate, impl_->destination, error);
        }

        impl_->file_handle = handle;
        impl_->temporary = candidate;
        return;
    }
#elif defined(__linux__)
    impl_->directory_fd =
        ::open(impl_->parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (impl_->directory_fd == -1) {
        const int error = errno;
        throw_path_error("Failed to open output parent directory",
                         impl_->parent, impl_->destination,
                         static_cast<unsigned long>(error));
    }

    if (!force) {
        struct stat status {};
        if (::fstatat(impl_->directory_fd,
                      impl_->destination_name.c_str(), &status,
                      AT_SYMLINK_NOFOLLOW) == 0) {
            throw std::runtime_error("Output destination already exists: '" +
                                     display_path(impl_->destination) + "'");
        }
        const int error = errno;
        if (error != ENOENT) {
            throw_path_error("Failed to inspect output destination",
                             impl_->parent / impl_->destination_name,
                             impl_->destination,
                             static_cast<unsigned long>(error));
        }
    }

    const std::uint64_t anonymous_counter =
        temporary_counter.fetch_add(1, std::memory_order_relaxed);
    impl_->temporary_name =
        make_candidate_name(impl_->destination_name, anonymous_counter,
                            "anonymous");
    impl_->temporary = impl_->parent / impl_->temporary_name;
    impl_->file_fd =
        ::openat(impl_->directory_fd, ".",
                 O_RDWR | O_TMPFILE | O_CLOEXEC, 0666);
    if (impl_->file_fd >= 0) {
        impl_->anonymous_tmpfile = true;
        return;
    }
    const int anonymous_error = errno;
    if (!tmpfile_is_unsupported(anonymous_error)) {
        throw_path_error("Failed to create anonymous Linux temporary output",
                         impl_->parent, impl_->destination,
                         static_cast<unsigned long>(anonymous_error));
    }

    for (std::uint64_t attempt = 0; attempt < kMaximumCreationAttempts;
         ++attempt) {
        const std::uint64_t counter =
            temporary_counter.fetch_add(1, std::memory_order_relaxed);
        impl_->temporary_name =
            make_candidate_name(impl_->destination_name, counter);
        impl_->temporary = impl_->parent / impl_->temporary_name;

        const int descriptor =
            ::openat(impl_->directory_fd, impl_->temporary_name.c_str(),
                     O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                     0666);
        if (descriptor == -1) {
            const int error = errno;
            if (error == EEXIST) {
                continue;
            }
            throw_path_error("Failed to exclusively create temporary output",
                             impl_->temporary, impl_->destination,
                             static_cast<unsigned long>(error));
        }

        impl_->file_fd = descriptor;
        impl_->temporary_linked = true;
        return;
    }
#endif

    throw std::runtime_error(
        "Failed to find a unique temporary output name beside destination '" +
        display_path(impl_->destination) + "'");
}

OutputTransaction::~OutputTransaction() noexcept = default;

const std::filesystem::path& OutputTransaction::temporary_path() const noexcept {
    return impl_->temporary;
}

int OutputTransaction::take_sink_fd() {
    if (impl_->sink_fd_taken) {
        throw std::logic_error(
            "The transactional output sink descriptor was already taken");
    }

#ifdef _WIN32
    HANDLE duplicate = INVALID_HANDLE_VALUE;
    HANDLE process = ::GetCurrentProcess();
    if (::DuplicateHandle(process, impl_->file_handle, process, &duplicate,
                          0, FALSE, DUPLICATE_SAME_ACCESS) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to duplicate temporary output identity",
                         impl_->temporary, impl_->destination, error);
    }

    const int descriptor = ::_open_osfhandle(
        reinterpret_cast<intptr_t>(duplicate), _O_BINARY | _O_RDWR);
    if (descriptor == -1) {
        const int conversion_error = errno;
        if (::CloseHandle(duplicate) == 0) {
            const DWORD close_error = ::GetLastError();
            throw std::runtime_error(
                "Failed to convert duplicated temporary output handle '" +
                display_path(impl_->temporary) + "' for destination '" +
                display_path(impl_->destination) + "': " +
                errno_message(conversion_error) +
                "; duplicate-handle cleanup also failed: " +
                system_message(close_error));
        }
        throw std::runtime_error(
            "Failed to convert duplicated temporary output handle '" +
            display_path(impl_->temporary) + "' for destination '" +
            display_path(impl_->destination) + "': " +
            errno_message(conversion_error));
    }
#elif defined(__linux__)
    const int descriptor =
        ::fcntl(impl_->file_fd, F_DUPFD_CLOEXEC, 0);
    if (descriptor == -1) {
        const int error = errno;
        throw_path_error("Failed to duplicate temporary output identity",
                         impl_->temporary, impl_->destination,
                         static_cast<unsigned long>(error));
    }
#endif

    impl_->sink_fd_taken = true;
    return descriptor;
}

void OutputTransaction::commit() {
    if (impl_->committed) {
        return;
    }

#ifdef _WIN32
    if (::FlushFileBuffers(impl_->file_handle) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to flush retained temporary output identity",
                         impl_->temporary, impl_->destination, error);
    }

    if (impl_->force) {
        verify_windows_path_identity(impl_->file_handle, impl_->temporary,
                                     impl_->destination);
        if (::MoveFileExW(impl_->temporary.c_str(),
                          impl_->destination.c_str(),
                          MOVEFILE_REPLACE_EXISTING |
                              MOVEFILE_WRITE_THROUGH) == 0) {
            const DWORD error = ::GetLastError();
            throw_path_error(
                "Failed to durably replace output from verified temporary identity",
                impl_->temporary, impl_->destination, error);
        }
        impl_->committed = true;
        return;
    }

    const std::filesystem::path::string_type& target =
        impl_->destination.native();
    if (target.size() >
        static_cast<std::size_t>(
            std::numeric_limits<DWORD>::max() / sizeof(wchar_t))) {
        throw std::runtime_error("Output destination path is too long for handle-based publication: '" +
                                 display_path(impl_->destination) + "'");
    }

    const std::size_t name_bytes = target.size() * sizeof(wchar_t);
    const std::size_t header_bytes = offsetof(FILE_RENAME_INFO, FileName);
    if (name_bytes > std::numeric_limits<std::size_t>::max() - header_bytes ||
        header_bytes + name_bytes >
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
        throw std::runtime_error("Output destination rename information is too large: '" +
                                 display_path(impl_->destination) + "'");
    }

    const std::size_t information_bytes = header_bytes + name_bytes;
    const std::size_t aligned_bytes =
        std::max(information_bytes, sizeof(FILE_RENAME_INFO));
    const std::size_t alignment_units =
        aligned_bytes / sizeof(std::max_align_t) +
        (aligned_bytes % sizeof(std::max_align_t) == 0 ? 0 : 1);
    std::vector<std::max_align_t> storage(alignment_units);
    auto* rename_info = ::new (static_cast<void*>(storage.data()))
        FILE_RENAME_INFO{};
    rename_info->ReplaceIfExists = FALSE;
    rename_info->RootDirectory = nullptr;
    rename_info->FileNameLength = static_cast<DWORD>(name_bytes);
    if (name_bytes != 0) {
        auto* const filename_storage =
            reinterpret_cast<unsigned char*>(storage.data()) + header_bytes;
        std::memcpy(filename_storage, target.data(), name_bytes);
    }

    if (::SetFileInformationByHandle(
            impl_->file_handle, FileRenameInfo, rename_info,
            static_cast<DWORD>(information_bytes)) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to publish retained temporary output identity",
                         impl_->temporary, impl_->destination, error);
    }
#elif defined(__linux__)
    if (!impl_->anonymous_tmpfile) {
        bool identity_matches = false;
        try {
            identity_matches = linux_named_identity_matches(
                impl_->file_fd, impl_->directory_fd,
                impl_->temporary_name, impl_->temporary,
                impl_->destination);
        } catch (...) {
            // If verification itself cannot establish ownership, pathname
            // cleanup could target a substituted entry. Retain only the fd.
            impl_->temporary_linked = false;
            throw;
        }
        if (!identity_matches) {
            impl_->temporary_linked = false;
            throw std::runtime_error(
                "Linked temporary output no longer names the retained file identity: '" +
                display_path(impl_->temporary) + "' for destination '" +
                display_path(impl_->destination) + "'");
        }
        if (!impl_->force) {
            if (::linkat(impl_->directory_fd,
                         impl_->temporary_name.c_str(),
                         impl_->directory_fd,
                         impl_->destination_name.c_str(), 0) != 0) {
                const int error = errno;
                throw_path_error(
                    "Failed to publish verified linked Linux temporary output",
                    impl_->temporary, impl_->destination,
                    static_cast<unsigned long>(error));
            }
            if (::unlinkat(impl_->directory_fd,
                           impl_->temporary_name.c_str(), 0) != 0) {
                const int error = errno;
                throw_path_error(
                    "Published output but failed to remove verified linked temporary output",
                    impl_->temporary, impl_->destination,
                    static_cast<unsigned long>(error));
            }
            impl_->temporary_linked = false;
        } else {
            if (::renameat(impl_->directory_fd,
                           impl_->temporary_name.c_str(),
                           impl_->directory_fd,
                           impl_->destination_name.c_str()) != 0) {
                const int error = errno;
                throw_path_error(
                    "Failed to atomically replace output from verified linked temporary output",
                    impl_->temporary, impl_->destination,
                    static_cast<unsigned long>(error));
            }
            impl_->temporary_linked = false;
        }
    } else if (!impl_->force) {
        const LinkIdentityResult result = link_anonymous_identity(
            impl_->file_fd, impl_->directory_fd,
            impl_->destination_name);
        if (!result.linked) {
            throw_link_identity_error(
                "Failed to publish anonymous Linux output identity",
                impl_->temporary, impl_->destination, result);
        }
    } else {
        bool staged = false;
        for (std::uint64_t attempt = 0;
             attempt < kMaximumCreationAttempts; ++attempt) {
            const std::uint64_t counter =
                temporary_counter.fetch_add(1, std::memory_order_relaxed);
            impl_->staging_name = make_candidate_name(
                impl_->destination_name, counter, "publish");
            const LinkIdentityResult result = link_anonymous_identity(
                impl_->file_fd, impl_->directory_fd,
                impl_->staging_name);
            if (result.linked) {
                staged = true;
                impl_->staging_linked = true;
                break;
            }
            if (result.error == EEXIST) {
                impl_->staging_name.clear();
                continue;
            }
            throw_link_identity_error(
                "Failed to stage anonymous Linux output identity",
                impl_->temporary,
                impl_->parent / impl_->staging_name,
                result);
        }
        if (!staged) {
            throw std::runtime_error(
                "Failed to find a unique Linux publication staging name beside destination '" +
                display_path(impl_->destination) + "'");
        }

        const std::filesystem::path staging_path =
            impl_->parent / impl_->staging_name;
        if (::renameat(impl_->directory_fd, impl_->staging_name.c_str(),
                       impl_->directory_fd,
                       impl_->destination_name.c_str()) != 0) {
            const int rename_error = errno;
            if (::unlinkat(impl_->directory_fd,
                           impl_->staging_name.c_str(), 0) != 0) {
                const int cleanup_error = errno;
                throw std::runtime_error(
                    "Failed to atomically replace destination '" +
                    display_path(impl_->destination) + "' from staging path '" +
                    display_path(impl_->parent / impl_->staging_name) +
                    "': " +
                    system_message(static_cast<unsigned long>(rename_error)) +
                    "; staging cleanup also failed: " +
                    system_message(static_cast<unsigned long>(cleanup_error)));
            }
            impl_->staging_name.clear();
            impl_->staging_linked = false;
            throw_path_error("Failed to atomically replace output from staging path",
                             staging_path, impl_->destination,
                             static_cast<unsigned long>(rename_error));
        }
        impl_->staging_name.clear();
        impl_->staging_linked = false;
    }
#endif

    impl_->committed = true;
}

}  // namespace ctb
