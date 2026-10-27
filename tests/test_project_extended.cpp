#include "../src/core.hpp"
#include <functional>
#include <iostream>
#include <iterator>
#include <random>
#include <thread>

using namespace chiplab;

namespace {
int checks = 0;
int failures = 0;
void check(bool value, const std::string& message) {
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
void rejects(const std::function<void()>& task, const std::string& message) {
    try { task(); check(false, message + " (accepted)"); }
    catch (const DomainError&) { check(true, message); }
    catch (const std::exception& error) { check(false, message + " (unexpected exception: " + error.what() + ")"); }
}
void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("Could not write test fixture");
}
std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not read test fixture");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
struct Folder {
    std::filesystem::path path;
    Folder() : path(std::filesystem::current_path() / (".project-extended-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directory(path);
    }
    ~Folder() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
const std::string valid = "{\"version\":1,\"architecture\":{\"name\":\"Teste\",\"bits\":8,\"registers\":8,\"memory_size\":64},\"source\":\"HALT\"}";
std::string replace_once(std::string text, const std::string& old_value, const std::string& new_value) {
    const auto pos = text.find(old_value);
    if (pos == std::string::npos) throw std::runtime_error("Invalid replacement fixture");
    text.replace(pos, old_value.size(), new_value);
    return text;
}
void check_project(const std::filesystem::path& file, const std::string& payload, const std::string& source, const std::string& message) {
    write_bytes(file, payload);
    try { check(load_project(file).second == source, message); }
    catch (const std::exception& error) { check(false, message + " (" + error.what() + ")"); }
}
bool no_temporaries(const std::filesystem::path& folder) {
    for (const auto& item : std::filesystem::directory_iterator(folder)) {
        if (item.path().filename().u8string().find(".chiplab-") != std::string::npos) return false;
    }
    return true;
}

void names_and_utf8() {
    for (const auto& name : {std::string{}, std::string(" \t\r\n"), std::string("CPU\nextra"), std::string("CPU\t2"), std::string("CPU\x7f"), std::string("CPU\xc2\x85"), std::string("\xc2\xa0"), std::string("\xe2\x80\x83\xe3\x80\x80")}) {
        Architecture architecture; architecture.name = name;
        rejects([&] { architecture.validate(); }, "reject control or whitespace-only architecture name");
    }
    for (unsigned control = 0; control < 32; ++control) {
        Architecture architecture; architecture.name = "CPU" + std::string(1, static_cast<char>(control));
        rejects([&] { architecture.validate(); }, "reject C0 name byte " + std::to_string(control));
    }
    const std::vector<std::string> malformed = {"\x80", "\xc0\xaf", "\xe0\x80\xaf", "\xf0\x80\x80\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xe2\x82", "\xc2", "\xe2X\xac", "\xff"};
    for (const auto& bytes : malformed) {
        Architecture architecture; architecture.name = bytes;
        rejects([&] { architecture.validate(); }, "reject malformed UTF-8 architecture");
        rejects([&] { detail::check_source(bytes); }, "reject malformed UTF-8 source");
    }
    for (const std::string& character : {std::string("a"), std::string("\xc3\xa1"), std::string("\xe7\x95\x8c"), std::string("\xf0\x9f\xa7\xa0")}) {
        Architecture architecture; architecture.name.clear();
        for (int i = 0; i < 60; ++i) architecture.name += character;
        architecture.validate();
        check(detail::utf8_length(architecture.name) == 60, "name length counts Unicode codepoints, not bytes");
        architecture.name += character;
        rejects([&] { architecture.validate(); }, "61 Unicode codepoints rejected");
    }
    Architecture normal; normal.name = "  CPU \xc3\x81 \xf0\x9f\x9a\x80  "; normal.validate();
    check(normal.name == "  CPU \xc3\x81 \xf0\x9f\x9a\x80  ", "valid name preserves surrounding spaces and Unicode");
}

void strict_json(const std::filesystem::path& file) {
    const std::vector<std::string> malformed = {
        valid + std::string("\0x", 2), "\xef\xbb" + valid, " " + std::string("\xef\xbb\xbf") + valid,
        std::string("\xef\xbb\xbf\xef\xbb\xbf") + valid, "\v" + valid, "\f" + valid,
        valid + "\v", valid + "//comment", "/*comment*/" + valid,
        replace_once(valid, "\"version\":1", "\"version\":+1"),
        replace_once(valid, "\"version\":1", "\"version\":01"),
        replace_once(valid, "\"version\":1", "\"version\":1.0"),
        replace_once(valid, "\"version\":1", "\"version\":1e0"),
        replace_once(valid, "\"version\":1", "\"version\":9223372036854775808"),
        replace_once(valid, "\"version\":1", "\"version\":-9223372036854775809"),
        replace_once(valid, "\"version\":1", "\"version\":true"),
        replace_once(valid, "\"version\":1", "\"version\":\"1\""),
        replace_once(valid, "\"bits\":8", "\"bits\":8,\"bits\":8"),
        replace_once(valid, "\"bits\":8", "\"b\\u0069ts\":8,\"bits\":8"),
        replace_once(valid, "\"architecture\":{", "\"architecture\":{\"extra\":1,"),
        replace_once(valid, "\"memory_size\":64", "\"memory_size\":\"64\""),
        replace_once(valid, "\"source\":\"HALT\"", "\"source\":{}"),
        replace_once(valid, "\"source\":\"HALT\"", "\"source\":null"),
        replace_once(valid, "\"source\":\"HALT\"", "\"source\":[]"),
        replace_once(valid, "\"HALT\"", "\"\\u0000\""),
        replace_once(valid, "\"HALT\"", "\"\\uD800\""),
        replace_once(valid, "\"HALT\"", "\"\\uDC00\""),
        replace_once(valid, "\"HALT\"", "\"\\uD800\\uD800\""),
        replace_once(valid, "\"HALT\"", "\"\\uD800x\""),
        replace_once(valid, "\"HALT\"", "\"\\uGGGG\""),
        replace_once(valid, "\"HALT\"", "\"\\q\""),
        replace_once(valid, "\"HALT\"", std::string("\"\xc0\xaf\"")),
        replace_once(valid, "\"HALT\"", std::string("\"\xed\xa0\x80\""))
    };
    for (const auto& payload : malformed) {
        write_bytes(file, payload);
        rejects([&] { (void)load_project(file); }, "strict JSON schema/syntax");
    }
    for (unsigned raw = 0; raw < 32; ++raw) {
        write_bytes(file, replace_once(valid, "\"HALT\"", "\"a" + std::string(1, static_cast<char>(raw)) + "b\""));
        rejects([&] { (void)load_project(file); }, "raw JSON control byte " + std::to_string(raw));
    }
    check_project(file, " \t\r\n" + valid + "\n\t ", "HALT", "standard JSON whitespace accepted");
    check_project(file, std::string("\xef\xbb\xbf") + valid, "HALT", "one leading UTF-8 BOM accepted");
    check_project(file, replace_once(valid, "\"HALT\"", "\"; \\u00E1 \\uD83D\\uDE80\\nHALT\\r\\n\""), "; \xc3\xa1 \xf0\x9f\x9a\x80\nHALT\r\n", "Unicode escapes and CRLF source roundtrip");
    check_project(file, replace_once(valid, "\"HALT\"", "\"; \\\" \\\\ \\/\\t\\n\""), "; \" \\ /\t\n", "JSON quote, slash, backslash, tab and LF escapes");
    for (const auto* field : {"bits", "registers", "memory_size"}) {
        const std::string old_value = std::string("\"") + field + "\":" + (std::string(field) == "memory_size" ? "64" : "8");
        for (const auto* value : {"-2147483649", "2147483648", "0", "null", "false", "{}", "[]"}) {
            write_bytes(file, replace_once(valid, old_value, std::string("\"") + field + "\":" + value));
            rejects([&] { (void)load_project(file); }, "invalid integer field");
        }
    }
    std::string nested = "0";
    for (int i = 0; i < 10000; ++i) nested = "{\"x\":" + nested + "}";
    rejects([&] { (void)detail::JsonParser(nested).parse(); }, "deep JSON bounded before stack growth");
}

void boundaries_and_safe_io(const std::filesystem::path& folder) {
    const auto file = folder / std::filesystem::u8path("projeto-\xc3\xa1-\xf0\x9f\x9a\x80.json");
    const std::string source = "; rascunho com aspas \" e \\, Unicode \xc3\xa1 \xf0\x9f\xa7\xa0\r\nMOV R0,";
    for (const int bits : {8, 16, 32}) for (const int registers : {4, 8, 16}) for (const int memory : {64, 128, 256}) {
        const Architecture architecture{"CPU \xc3\x81 \xf0\x9f\x9a\x80", bits, registers, memory};
        save_project(file, architecture, source);
        const auto loaded = load_project(file);
        check(loaded.first.name == architecture.name && loaded.first.bits == bits && loaded.first.registers == registers && loaded.first.memory_size == memory && loaded.second == source, "27 architecture combinations with Unicode path and draft");
    }
    for (const auto& draft : {std::string{}, std::string("\n\t\r\n"), std::string("MOV R99,"), std::string(MAX_SOURCE_LENGTH, 'x'), std::string(MAX_SOURCE_LENGTH / 2, '\n')}) {
        save_project(file, {}, draft);
        check(load_project(file).second == draft, "empty, incomplete and maximum source drafts saved losslessly");
    }
    std::string multibyte;
    for (std::size_t i = 0; i < MAX_SOURCE_LENGTH / 2; ++i) multibyte += "\xc3\xa1";
    save_project(file, {}, multibyte);
    check(load_project(file).second == multibyte, "source exact UTF-8 byte limit accepted");
    const auto previous = read_bytes(file);
    rejects([&] { save_project(file, {}, multibyte + "x"); }, "source byte limit +1 rejected");
    check(read_bytes(file) == previous && no_temporaries(folder), "invalid source does not overwrite or leave temporary");
    rejects([&] { save_project(file, {}, std::string("HALT\0", 5)); }, "NUL source rejected before overwrite");
    check(read_bytes(file) == previous && no_temporaries(folder), "NUL source leaves prior file intact");
    Architecture bad; bad.name = std::string(61, 'n');
    rejects([&] { save_project(file, bad, "HALT"); }, "bad architecture rejected before overwrite");
    check(read_bytes(file) == previous, "bad architecture preserves previous bytes");
    rejects([&] { save_project(folder / "missing" / "file.json", {}, "HALT"); }, "missing parent directory");
    rejects([&] { save_project(folder, {}, "HALT"); }, "directory target rejected");
    rejects([&] { (void)load_project(folder / "missing.json"); }, "missing file rejected");
    rejects([&] { (void)load_project(folder); }, "directory input rejected");
    check(no_temporaries(folder), "no temporary after failed I/O");
#ifdef _WIN32
    const auto original_attributes = GetFileAttributesW(file.c_str());
    check(original_attributes != INVALID_FILE_ATTRIBUTES && SetFileAttributesW(file.c_str(), original_attributes | FILE_ATTRIBUTE_READONLY), "readonly fixture initialized");
    rejects([&] { save_project(file, {}, "HALT"); }, "read-only destination rejected");
    check(read_bytes(file) == previous && no_temporaries(folder), "read-only save preserves bytes and removes temporary");
    check(SetFileAttributesW(file.c_str(), original_attributes) != 0, "readonly fixture restored");
    HANDLE locked = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "locked fixture initialized");
    if (locked != INVALID_HANDLE_VALUE) {
        rejects([&] { save_project(file, {}, "HALT"); }, "locked destination rejected");
        CloseHandle(locked);
        check(read_bytes(file) == previous && no_temporaries(folder), "locked save preserves bytes and removes temporary");
    }
#endif
    const auto size_file = folder / "size.json";
    write_bytes(size_file, valid + std::string(MAX_PROJECT_BYTES - valid.size(), ' '));
    check(load_project(size_file).second == "HALT", "exact maximum project bytes accepted");
    write_bytes(size_file, valid + std::string(MAX_PROJECT_BYTES + 1 - valid.size(), ' '));
    rejects([&] { (void)load_project(size_file); }, "maximum project bytes +1 rejected");
    check(read_bytes(file) == previous, "failed loads never alter saved project");
}

void atomic_raw_bytes_and_concurrency(const std::filesystem::path& folder) {
    const auto file = folder / "export.bin";
    detail::write_file_atomic(file, "old export");
    const std::string binary("a\0b\r\n\xff", 6);
    detail::write_file_atomic(file, binary);
    check(read_bytes(file) == binary && no_temporaries(folder), "atomic helper preserves all binary bytes");
    detail::write_file_atomic(file, {});
    check(read_bytes(file).empty() && no_temporaries(folder), "empty atomic export replaces existing file");
    const std::string large(3 * 1048576 + 17, 'V');
    detail::write_file_atomic(file, large);
    check(read_bytes(file) == large && no_temporaries(folder), "multi-chunk raw export is not limited by project JSON size");
#ifdef _WIN32
    HANDLE locked = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "binary export lock fixture");
    if (locked != INVALID_HANDLE_VALUE) {
        rejects([&] { detail::write_file_atomic(file, "replacement"); }, "blocked raw export cannot truncate prior file");
        CloseHandle(locked);
        check(read_bytes(file) == large && no_temporaries(folder), "blocked raw export preserves large existing bytes");
    }
#endif
    const auto shared = folder / "shared.json";
    std::atomic<unsigned> completed{0};
    std::atomic<unsigned> errors{0};
    std::vector<std::thread> writers;
    for (unsigned worker = 0; worker < 6; ++worker) {
        writers.emplace_back([&, worker] {
            for (unsigned sample = 0; sample < 12; ++sample) {
                try {
                    const auto label = "Writer " + std::to_string(worker) + " sample " + std::to_string(sample);
                    save_project(shared, Architecture{label, 8, 8, 64}, "; " + label + "\nHALT");
                    ++completed;
                } catch (const DomainError&) { ++errors; }
            }
        });
    }
    for (auto& writer : writers) writer.join();
    // Windows may reject concurrent replacements of the same target with a
    // sharing violation. The contract is complete bytes or DomainError, never
    // a partially overwritten original; automatic retries are not promised.
    check(completed > 0 && completed + errors == 72, "72 parallel save attempts finish with success or a safe I/O error");
    std::cout << "Concurrent save attempts: " << completed << " succeeded, " << errors << " safe I/O rejections\n";
    const auto loaded = load_project(shared);
    check(loaded.second == "; " + loaded.first.name + "\nHALT", "parallel replacement leaves one complete consistent project");
    check(no_temporaries(folder), "parallel saves clean all temporary files");
}

std::string canonical(const detail::JsonValue& value) {
    if (value.type == detail::JsonValue::Type::String) return detail::json_string(value.string);
    if (value.type == detail::JsonValue::Type::Integer) return std::to_string(value.integer);
    std::string output = "{";
    for (const auto& pair : value.object) {
        if (output.size() > 1) output += ',';
        output += detail::json_string(pair.first) + ":" + canonical(pair.second);
    }
    return output + "}";
}
void deterministic_fuzz(const std::filesystem::path& file) {
    std::mt19937 random(0xC11B2026u);
    // Parser fuzz is entirely bounded and reproducible. DomainError is expected;
    // accepted strings must serialize and reparse without losing any value.
    for (unsigned sample = 0; sample < 12000; ++sample) {
        std::string payload;
        if (sample % 2 == 0) {
            payload = valid;
            const unsigned mutations = 1 + random() % 6;
            for (unsigned i = 0; i < mutations; ++i) {
                const auto pos = random() % (payload.size() + 1);
                if (pos < payload.size() && random() % 3 == 0) payload.erase(pos, 1);
                else payload.insert(pos, 1, static_cast<char>(random() % 256));
            }
        } else {
            const auto count = random() % 200;
            for (unsigned i = 0; i < count; ++i) payload += static_cast<char>(random() % 256);
        }
        try {
            const auto parsed = detail::JsonParser(payload).parse();
            const auto serialized = canonical(parsed);
            check(canonical(detail::JsonParser(serialized).parse()) == serialized, "accepted fuzz input roundtrip");
        } catch (const DomainError&) { check(true, "malformed fuzz input rejected safely"); }
        catch (const std::exception& error) { check(false, std::string("unexpected parser fuzz exception: ") + error.what()); }
    }
    const std::vector<std::string> chars = {"a", " ", "\t", "\r", "\n", "\"", "\\", "/", ";", "\xc3\xa1", "\xf0\x9f\x9a\x80"};
    for (unsigned sample = 0; sample < 100; ++sample) {
        std::string draft;
        const auto length = random() % 200;
        for (unsigned i = 0; i < length; ++i) draft += chars[random() % chars.size()];
        Architecture architecture{"CPU " + std::to_string(sample), (sample % 3 == 0 ? 8 : sample % 3 == 1 ? 16 : 32), 8, 64};
        save_project(file, architecture, draft);
        const auto first = load_project(file);
        save_project(file, first.first, first.second);
        const auto second = load_project(file);
        check(first.first.name == architecture.name && second.first.bits == architecture.bits && first.second == draft && second.second == draft, "100 generated draft save/load/re-save roundtrips");
    }
}
} // namespace

int main() {
    Folder folder;
    const auto file = folder.path / "project.json";
    try {
        names_and_utf8(); std::cout << "DONE architecture names and UTF-8\n";
        strict_json(file); std::cout << "DONE strict schema and JSON syntax\n";
        boundaries_and_safe_io(folder.path); std::cout << "DONE boundaries, paths, atomic save and I/O\n";
        atomic_raw_bytes_and_concurrency(folder.path); std::cout << "DONE binary exports and 72 concurrent saves\n";
        deterministic_fuzz(file); std::cout << "DONE 12000 parser mutations and 100 draft roundtrips\n";
    } catch (const std::exception& error) { ++failures; std::cerr << "UNEXPECTED " << error.what() << '\n'; }
    std::cout << checks << " checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}
