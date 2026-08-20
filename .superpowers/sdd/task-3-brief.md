### Task 3: Author HTSlib Text and BGZF I/O Wrappers

Create `include/convert_to_biallelic/vcf_io.hpp` and `src/vcf_io.cpp`. Source-only: install nothing; do not compile, execute, test, benchmark, use Git, or alter Python.

Required API in namespace `ctb`:

```cpp
class InputSource {
public:
    InputSource(const std::filesystem::path&, std::size_t io_workers);
    ~InputSource();
    InputSource(InputSource&&) noexcept;
    InputSource& operator=(InputSource&&) noexcept;
    bool getline(std::string& line);
    bool compressed() const noexcept;
private: struct Impl; std::unique_ptr<Impl> impl_;
};

class OutputSink {
public:
    OutputSink(const std::filesystem::path&, OutputFormat, std::size_t io_workers);
    ~OutputSink();
    OutputSink(OutputSink&&) noexcept;
    OutputSink& operator=(OutputSink&&) noexcept;
    void write(std::string_view bytes);
    void flush();
    void close();
private: struct Impl; std::unique_ptr<Impl> impl_;
};
```

Implementation requirements:

1. Include official public HTSlib headers for `htsFile`, `kstring_t`, hFILE, and BGZF.
2. Input uses `hts_open(path, "r")`; accepts only text VCF input with no compression, gzip, or BGZF; rejects BCF and unrelated formats with path-specific error.
3. If compressed and `io_workers > 0`, call `hts_set_threads`; failure throws.
4. Read with `hts_getline(file, KS_SEP_LINE, &line)`; `-1` EOF; below `-1` error; copy exact returned bytes to `std::string` without terminator.
5. `compressed()` reflects detected gzip/BGZF compression.
6. Plain output uses `hopen(path, "wb")`, loops `hwrite` until every byte is written, flushes with `hflush`, closes with `hclose`.
7. Compressed output uses `bgzf_open(path, "w")`, calls `bgzf_mt` if `io_workers > 0`, loops `bgzf_write`, flushes and closes with checked return values.
8. Output format is the explicit `OutputFormat`, never inferred from temporary path.
9. Output `close()` is idempotent and propagates flush/close failures. Destructors never throw and close still-open handles best-effort. Move operations transfer ownership safely.
10. Reject operations on moved-from/closed objects with descriptive exception.
11. Use path strings safely on Windows through `std::filesystem::path`; where HTSlib requires `char*`, use UTF-8 conversion with an isolated helper and record Unicode-path portability as unverified.
12. Static self-review ownership, error paths, and public signature consistency. All HTSlib API/build/runtime behavior remains unverified.

Use apply_patch. Write `.superpowers/sdd/task-3-report.md` and a complete `.superpowers/sdd/task-3-review-package.md` containing exact full new-file contents. No tests/commits. Return concise status and concerns.
