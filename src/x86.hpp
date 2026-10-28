#ifndef CHIPLAB_X86_HPP
#define CHIPLAB_X86_HPP

#include "core.hpp"
#include <array>
#include <memory>
#include <iomanip>
#include <cstring>
#include <regex>
#include <thread>
#include "../third_party/include/unicorn/unicorn.h"
#include "../third_party/include/unicorn/x86.h"
#include "../third_party/include/capstone/capstone.h"

namespace chiplab { namespace x86 {
namespace fs = std::filesystem;
constexpr std::uint32_t MEMORY_BASE = 0x1000;
constexpr std::uint32_t MEMORY_SIZE = 2 * 1024 * 1024;
constexpr std::uint32_t STACK_TOP = MEMORY_BASE + MEMORY_SIZE - 16;
struct Program {
    std::string source;
    std::vector<std::uint8_t> machine_code;
    std::uint32_t base = MEMORY_BASE, entry = MEMORY_BASE;
    std::string listing;
};
struct Snapshot {
    std::array<std::uint32_t, 8> regs{};
    std::uint32_t eip = MEMORY_BASE, eflags = 2;
};
struct TraceEntry {
    std::uint64_t cycle = 0;
    std::uint32_t pc = 0, next_pc = 0;
    std::string instruction, bytes;
    std::map<std::string, std::uint32_t> register_changes;
    std::map<std::uint32_t, std::uint8_t> memory_changes;
    bool halted = false;
};
inline fs::path runtime_directory();
inline Program assemble(const std::string&, const fs::path& runtimeDir);
inline bool is_project(const std::string&);
inline std::pair<std::string, std::string> parse_project(const std::string&);
inline std::string project_json(const std::string& name, const std::string& source);
class CPU {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Program program;
    std::uint64_t cycles = 0;
    bool halted = false;
    std::string output, halt_reason;
    std::vector<TraceEntry> trace;
    CPU(Program, const fs::path& runtimeDir);
    ~CPU();
    CPU(CPU&&) noexcept;
    CPU& operator=(CPU&&) noexcept;
    CPU(const CPU&) = delete;
    CPU& operator=(const CPU&) = delete;
    Snapshot snapshot() const;
    std::uint32_t pc() const;
    std::vector<std::uint8_t> readMemory(std::uint32_t, std::size_t) const;
    std::string nextInstruction() const;
    TraceEntry step();
    void run(std::size_t);
};

namespace implementation {
constexpr std::size_t MAX_BINARY = 1024 * 1024;
constexpr std::size_t MAX_OUTPUT = 65536;
constexpr std::size_t MAX_STEP_WRITES = 262144;
inline std::string hex(std::uint32_t value) {
    std::ostringstream out; out << "0x" << std::hex << std::uppercase << value; return out.str();
}
inline std::string readFile(const fs::path& path, std::size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw DomainError("Nao foi possivel ler o arquivo gerado pelo NASM.");
    std::string value(limit + 1, '\0');
    in.read(&value[0], static_cast<std::streamsize>(value.size()));
    if (in.bad()) throw DomainError("Falha de leitura do arquivo gerado pelo NASM.");
    value.resize(static_cast<std::size_t>(in.gcount()));
    if (value.size() > limit) throw DomainError("O arquivo gerado excedeu o limite permitido.");
    return value;
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE v) : value(v) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct TemporaryDirectory {
    fs::path path;
    TemporaryDirectory() {
        wchar_t buffer[32768];
        const auto length = GetTempPathW(static_cast<DWORD>(std::size(buffer)), buffer);
        if (!length || length >= std::size(buffer)) throw DomainError("Pasta temporaria indisponivel.");
        static std::atomic<unsigned long long> serial{0};
        for (int attempt = 0; attempt < 100; ++attempt) {
            path = fs::path(buffer) / (L"ChipLab-x86-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(serial++));
            std::error_code error;
            if (fs::create_directory(path, error)) return;
        }
        throw DomainError("Nao foi possivel criar a pasta temporaria do NASM.");
    }
    ~TemporaryDirectory() { std::error_code ignored; fs::remove_all(path, ignored); }
};
inline std::wstring quote(const std::wstring& value) {
    // Paths cannot contain a quote on Windows. The NASM arguments used here have no trailing slash.
    return L"\"" + value + L"\"";
}
inline void nasmProcess(const fs::path& executable, const fs::path& directory,
                        const std::wstring& arguments, const fs::path& diagnostics,
                        const fs::path& binary, const fs::path& listing) {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle log(CreateFileW(diagnostics.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (log.value == INVALID_HANDLE_VALUE || input.value == INVALID_HANDLE_VALUE)
        throw DomainError("Nao foi possivel preparar a execucao do NASM.");
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = input.value; startup.hStdOutput = log.value; startup.hStdError = log.value;
    PROCESS_INFORMATION process{};
    std::wstring command = quote(executable.wstring()) + L" " + arguments;
    if (!CreateProcessW(executable.c_str(), &command[0], nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process))
        throw DomainError("Nao foi possivel iniciar runtime/nasm.exe. Confira os arquivos do programa.");
    Handle processHandle(process.hProcess), threadHandle(process.hThread);
    const auto start = GetTickCount64();
    bool excessive = false, timedOut = false;
    for (;;) {
        const auto status = WaitForSingleObject(process.hProcess, 25);
        if (status == WAIT_OBJECT_0) break;
        if (status != WAIT_TIMEOUT) { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, INFINITE); throw DomainError("Falha ao aguardar o NASM."); }
        std::error_code ignored;
        const auto tooLarge = [&](const fs::path& p, std::uintmax_t maximum) {
            const auto length = fs::file_size(p, ignored); return !ignored && length > maximum;
        };
        excessive = tooLarge(diagnostics, MAX_BINARY) || tooLarge(binary, MAX_BINARY + 4096) || tooLarge(listing, 8 * MAX_BINARY);
        timedOut = GetTickCount64() - start >= 5000;
        if (excessive || timedOut) { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, INFINITE); break; }
    }
    if (timedOut) throw DomainError("O NASM excedeu o limite de 5 segundos. Simplifique o codigo ou as macros.");
    if (excessive) throw DomainError("O NASM excedeu o limite de tamanho dos arquivos gerados.");
    DWORD result = 1; GetExitCodeProcess(process.hProcess, &result);
    if (result != 0) {
        auto message = readFile(diagnostics, MAX_BINARY);
        if (message.size() > 16000) message.resize(16000);
        throw DomainError("Erro de montagem NASM:\n" + message);
    }
}
inline std::string stripComments(const std::string& source) {
    std::string cleaned;
    bool quoted = false; char quoteChar = 0;
    for (std::size_t pos = 0; pos < source.size(); ++pos) {
        const char c = source[pos];
        if (c == '\n') { quoted = false; cleaned += c; continue; }
        if (quoted) { if (c == quoteChar) quoted = false; cleaned += ' '; continue; }
        if (c == '\'' || c == '"' || c == '`') { quoted = true; quoteChar = c; cleaned += ' '; continue; }
        if (c == ';') { while (pos < source.size() && source[pos] != '\n') ++pos; cleaned += '\n'; continue; }
        cleaned += c;
    }
    return cleaned;
}
inline void validateDirectives(const std::string& source, bool expanded = false) {
    const auto plain = stripComments(source);
    if (std::regex_search(plain, std::regex(R"(\b(include|incbin)\b)", std::regex::icase)))
        throw DomainError("%include e INCBIN nao sao suportados: mantenha todo o fonte no editor.");
    if (std::regex_search(plain, std::regex(R"(\[\s*map\b)", std::regex::icase)))
        throw DomainError("MAP e reservado ao montador interno do ChipLab.");
    if (std::regex_search(plain, std::regex(R"(\b(vstart|start|vfollows)\s*=)", std::regex::icase)))
        throw DomainError("Enderecos personalizados de secao (start/vstart/vfollows) nao sao suportados. Use as secoes NASM com o posicionamento padrao.");
    if (std::regex_search(plain, std::regex(R"(\b(bits|use16|use64)\s*(16|64)?\b)", std::regex::icase))) {
        std::istringstream lines(plain); std::string line;
        const std::regex bits(R"(\bbits\s+([^\]\s]+))", std::regex::icase);
        while (std::getline(lines, line)) {
            std::smatch match;
            if (std::regex_search(line, match, bits) && match[1].str() != "32")
                throw DomainError("Este modo executa apenas BITS 32 (x86 de 32 bits).");
            if (std::regex_search(line, std::regex(R"(\buse(16|64)\b)", std::regex::icase)))
                throw DomainError("USE16 e USE64 nao sao suportados no modo x86 de 32 bits.");
        }
    }
    // ORG is supplied by ChipLab. Duplicate ORG is rejected to keep addresses unambiguous.
    if (!expanded && std::regex_search(plain, std::regex(R"(\borg\b)", std::regex::icase)))
        throw DomainError("Remova ORG do fonte: o ChipLab utiliza a origem fixa 0x1000.");
    if (expanded) {
        const std::regex org(R"(\borg\s+([^\]\s]+))", std::regex::icase);
        for (auto it = std::sregex_iterator(plain.begin(), plain.end(), org); it != std::sregex_iterator(); ++it)
            if ((*it)[1].str() != "0x1000") throw DomainError("ORG alterado por macro: este ambiente utiliza a origem fixa 0x1000.");
    }
}
struct Module {
    HMODULE value = nullptr;
    Module(const fs::path& runtime, const wchar_t* file) {
        const auto path = fs::absolute(runtime / file);
        value = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!value) throw DomainError("Biblioteca x86 ausente ou incompativel: " + path.filename().string() + ". Extraia toda a pasta ChipLab.");
    }
    ~Module() { if (value) FreeLibrary(value); }
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    template<class T> T symbol(const char* name) {
        const auto address = GetProcAddress(value, name);
        if (!address) throw DomainError(std::string("Biblioteca x86 sem a funcao necessaria: ") + name);
        T function = nullptr;
        static_assert(sizeof(function) == sizeof(address), "Windows function pointer size mismatch");
        std::memcpy(&function, &address, sizeof(function));
        return function;
    }
};
struct API {
    Module unicorn, capstone;
#define CHIPLAB_UC_FUNCTION(name) decltype(&::name) name = unicorn.symbol<decltype(&::name)>(#name)
    CHIPLAB_UC_FUNCTION(uc_open);
    CHIPLAB_UC_FUNCTION(uc_close);
    CHIPLAB_UC_FUNCTION(uc_mem_map);
    CHIPLAB_UC_FUNCTION(uc_mem_read);
    CHIPLAB_UC_FUNCTION(uc_mem_write);
    CHIPLAB_UC_FUNCTION(uc_reg_read);
    CHIPLAB_UC_FUNCTION(uc_reg_write);
    CHIPLAB_UC_FUNCTION(uc_emu_start);
    CHIPLAB_UC_FUNCTION(uc_emu_stop);
    CHIPLAB_UC_FUNCTION(uc_hook_add);
    CHIPLAB_UC_FUNCTION(uc_query);
    CHIPLAB_UC_FUNCTION(uc_context_alloc);
    CHIPLAB_UC_FUNCTION(uc_context_save);
    CHIPLAB_UC_FUNCTION(uc_context_restore);
    CHIPLAB_UC_FUNCTION(uc_context_free);
    CHIPLAB_UC_FUNCTION(uc_strerror);
#undef CHIPLAB_UC_FUNCTION
#define CHIPLAB_CS_FUNCTION(name) decltype(&::name) name = capstone.symbol<decltype(&::name)>(#name)
    CHIPLAB_CS_FUNCTION(cs_open);
    CHIPLAB_CS_FUNCTION(cs_close);
    CHIPLAB_CS_FUNCTION(cs_disasm);
    CHIPLAB_CS_FUNCTION(cs_free);
    CHIPLAB_CS_FUNCTION(cs_option);
#undef CHIPLAB_CS_FUNCTION
    explicit API(const fs::path& runtime) : unicorn(runtime, L"unicorn.dll"), capstone(runtime, L"capstone.dll") {}
    void check(uc_err error, const std::string& context) const {
        if (error != UC_ERR_OK) throw DomainError(context + ": " + uc_strerror(error));
    }
};
inline bool validRange(std::uint32_t address, std::size_t size) {
    return address >= MEMORY_BASE && size <= MEMORY_SIZE &&
        static_cast<std::uint64_t>(address) + size <= static_cast<std::uint64_t>(MEMORY_BASE) + MEMORY_SIZE;
}
inline const std::array<int, 8>& registerIds() {
    static const std::array<int, 8> ids{{UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
        UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP}};
    return ids;
}
inline const std::array<const char*, 8>& registerNames() {
    static const std::array<const char*, 8> names{{"EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP", "ESP"}};
    return names;
}
} // namespace implementation

inline fs::path runtime_directory() {
    std::vector<wchar_t> path(32768);
    const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) throw DomainError("Nao foi possivel localizar o diretorio do programa.");
    return fs::path(std::wstring(path.data(), size)).parent_path() / "runtime";
}
inline Program assemble(const std::string& source, const fs::path& runtimeDir) {
    chiplab::detail::check_source(source);
    if (chiplab::detail::trim(source).empty()) throw DomainError("Digite um programa Assembly x86 antes de montar.");
    implementation::validateDirectives(source);
    implementation::TemporaryDirectory temp;
    const auto sourceFile = temp.path / "program.asm", binary = temp.path / "program.bin";
    const auto listing = temp.path / "program.lst", diagnostics = temp.path / "errors.txt";
    const auto expanded = temp.path / "expanded.asm";
    const auto symbols = temp.path / "symbols.map";
    const auto executable = fs::absolute(runtimeDir / "nasm.exe");
    if (!fs::is_regular_file(executable)) throw DomainError("runtime/nasm.exe nao foi encontrado.");
    { std::ofstream out(sourceFile, std::ios::binary); out << "BITS 32\nORG 0x1000\n%line 0+1 editor.asm\n" << source << '\n';
      if (!out) throw DomainError("Nao foi possivel gravar o fonte temporario."); }
    // Check the preprocessed form too: a macro must not silently switch BITS or generate INCBIN.
    implementation::nasmProcess(executable, temp.path,
        L"-E -f bin -o expanded.asm program.asm", diagnostics, expanded, listing);
    auto expandedSource = implementation::readFile(expanded, implementation::MAX_BINARY);
    implementation::validateDirectives(expandedSource, true);
    // NASM 3.02 advances %line before the following source line. The second assembly pass
    // would advance the preprocessor's emitted coordinates again; compensate once here.
    {
        std::istringstream lines(expandedSource); std::string line, normalized;
        const std::regex coordinate(R"(^%line ([0-9]+)\+([0-9]+) editor\.asm$)");
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::smatch match;
            if (std::regex_match(line, match, coordinate)) {
                const auto number = std::stoull(match[1].str()), increment = std::stoull(match[2].str());
                line = "%line " + std::to_string(number >= increment ? number - increment : 0) + "+" + match[2].str() + " editor.asm";
            }
            normalized += line + '\n';
        }
        expandedSource = std::move(normalized);
    }
    // NASM's map resolves labels (including macro-generated labels) without adding bytes or
    // moving .bss/data addresses. Append after preprocessing so user macros cannot rewrite it.
    { std::ofstream out(expanded, std::ios::binary); out << expandedSource << "\n[map symbols symbols.map]\n";
      if (!out) throw DomainError("Nao foi possivel preparar o mapa de simbolos NASM."); }
    implementation::nasmProcess(executable, temp.path,
        L"-f bin -o program.bin -l program.lst expanded.asm", diagnostics, binary, listing);
    const auto assembled = implementation::readFile(binary, implementation::MAX_BINARY);
    if (assembled.empty()) throw DomainError("O programa deve gerar entre 1 byte e 1 MiB de codigo/dados.");
    Program result; result.source = source;
    result.machine_code.assign(assembled.begin(), assembled.end());
    const auto map = implementation::readFile(symbols, 8 * implementation::MAX_BINARY);
    std::map<std::string, std::uint32_t> entries;
    const std::regex symbol(R"(^\s*([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(_start|start|main)\s*$)");
    std::istringstream mapLines(map); std::string mapLine;
    while (std::getline(mapLines, mapLine)) {
        std::smatch match;
        if (std::regex_match(mapLine, match, symbol)) {
            const auto value = std::stoull(match[2].str(), nullptr, 16);
            if (value > UINT32_MAX) throw DomainError("Simbolo de entrada fora do endereco x86 de 32 bits.");
            entries[match[3].str()] = static_cast<std::uint32_t>(value);
        }
    }
    for (const char* candidate : {"_start", "start", "main"}) {
        const auto found = entries.find(candidate);
        if (found != entries.end()) { result.entry = found->second; break; }
    }
    if (result.entry < result.base || result.entry >= result.base + result.machine_code.size())
        throw DomainError("A entrada _start/start/main deve apontar para os bytes do programa.");
    result.listing = implementation::readFile(listing, 8 * implementation::MAX_BINARY);
    return result;
}

inline bool is_project(const std::string& text) {
    if (text.size() > MAX_PROJECT_BYTES) throw DomainError("O projeto excede o limite de 1 MiB.");
    const auto document = chiplab::detail::JsonParser(text).parse();
    if (document.type != chiplab::detail::JsonValue::Type::Object) throw DomainError("Projeto invalido: o JSON deve ser um objeto.");
    const auto mode = document.object.find("mode");
    const auto version = document.object.find("version");
    return (mode != document.object.end() && mode->second.type == chiplab::detail::JsonValue::Type::String && mode->second.string == "x86-32") ||
        (version != document.object.end() && version->second.type == chiplab::detail::JsonValue::Type::Integer && version->second.integer == 2);
}
inline std::pair<std::string, std::string> parse_project(const std::string& text) {
    if (text.size() > MAX_PROJECT_BYTES) throw DomainError("O projeto excede o limite de 1 MiB.");
    const auto document = chiplab::detail::JsonParser(text).parse();
    chiplab::detail::keys(document, {"version", "mode", "name", "source"});
    if (chiplab::detail::int_field(document, "version") != 2 || chiplab::detail::string_field(document, "mode") != "x86-32")
        throw DomainError("Projeto incompativel: o modo x86 usa version 2 e mode x86-32.");
    auto name = chiplab::detail::string_field(document, "name"), source = chiplab::detail::string_field(document, "source");
    chiplab::detail::check_architecture_name(name); chiplab::detail::check_source(source);
    return {name, source};
}
inline std::string project_json(const std::string& name, const std::string& source) {
    chiplab::detail::check_architecture_name(name); chiplab::detail::check_source(source);
    const auto result = std::string("{\n  \"version\": 2,\n  \"mode\": \"x86-32\",\n  \"name\": ") +
        chiplab::detail::json_string(name) + ",\n  \"source\": " + chiplab::detail::json_string(source) + "\n}\n";
    if (result.size() > MAX_PROJECT_BYTES) throw DomainError("O projeto excede o limite de 1 MiB.");
    return result;
}

struct CPU::Impl {
    implementation::API api;
    uc_engine* engine = nullptr;
    csh disassembler = 0;
    uc_hook writeHook = 0;
    std::map<std::uint32_t, std::uint8_t> oldMemory;
    bool tracking = false, writeLimit = false;
    explicit Impl(const fs::path& runtime) : api(runtime) {
        api.check(api.uc_open(UC_ARCH_X86, UC_MODE_32, &engine), "Falha ao criar a CPU x86");
        try {
            api.check(api.uc_mem_map(engine, MEMORY_BASE, MEMORY_SIZE, UC_PROT_ALL), "Falha ao mapear a memoria x86");
            if (api.cs_open(CS_ARCH_X86, CS_MODE_32, &disassembler) != CS_ERR_OK) throw DomainError("Falha ao criar o decodificador x86.");
            api.cs_option(disassembler, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
            api.check(api.uc_hook_add(engine, &writeHook, UC_HOOK_MEM_WRITE, reinterpret_cast<void*>(&Impl::onWrite), this, 1, 0),
                "Falha ao observar a memoria x86");
        } catch (...) { if (disassembler) api.cs_close(&disassembler); api.uc_close(engine); engine = nullptr; throw; }
    }
    ~Impl() { if (disassembler) api.cs_close(&disassembler); if (engine) api.uc_close(engine); }
    static void onWrite(uc_engine*, uc_mem_type, std::uint64_t address, int size, std::int64_t, void* user) noexcept {
        auto& self = *static_cast<Impl*>(user);
        if (!self.tracking || size <= 0) return;
        try {
            // A write crossing an unmapped boundary may partially modify the mapped page before failing.
            // Save the mapped intersection as well, so the transaction can restore those bytes.
            const auto first = (std::max)(address, static_cast<std::uint64_t>(MEMORY_BASE));
            const auto last = (std::min)(address + static_cast<unsigned>(size), static_cast<std::uint64_t>(MEMORY_BASE) + MEMORY_SIZE);
            if (first >= last) return;
            const auto count = static_cast<std::size_t>(last - first);
            const bool exceeds = self.oldMemory.size() + count > implementation::MAX_STEP_WRITES;
            std::vector<std::uint8_t> before(count);
            if (self.api.uc_mem_read(self.engine, first, before.data(), before.size()) != UC_ERR_OK) return;
            for (std::size_t i = 0; i < count; ++i) self.oldMemory.emplace(static_cast<std::uint32_t>(first + i), before[i]);
            // Even the write that triggers the limit must be saved: Unicorn may complete it after stop.
            if (exceeds) { self.writeLimit = true; self.api.uc_emu_stop(self.engine); }
        } catch (...) { self.writeLimit = true; self.api.uc_emu_stop(self.engine); }
    }
    struct Decoded {
        unsigned id = 0;
        std::uint16_t size = 0;
        std::string text, bytes;
        bool repeated = false;
    };
    Decoded decode(std::uint32_t address) const {
        if (!implementation::validRange(address, 1)) throw DomainError("EIP fora da memoria: " + implementation::hex(address));
        const auto length = (std::min)(static_cast<std::size_t>(15), static_cast<std::size_t>(MEMORY_BASE + MEMORY_SIZE - address));
        std::array<std::uint8_t, 15> bytes{};
        api.check(api.uc_mem_read(engine, address, bytes.data(), length), "Falha ao ler a instrucao x86");
        cs_insn* instruction = nullptr;
        if (api.cs_disasm(disassembler, bytes.data(), length, address, 1, &instruction) != 1)
            throw DomainError("Bytes invalidos para uma instrucao x86 em " + implementation::hex(address));
        Decoded result; result.id = instruction->id; result.size = instruction->size;
        result.text = instruction->mnemonic;
        if (instruction->op_str[0]) result.text += std::string(" ") + instruction->op_str;
        switch (instruction->id) {
            case X86_INS_MOVSB: case X86_INS_MOVSW: case X86_INS_MOVSD: case X86_INS_MOVSQ:
            case X86_INS_CMPSB: case X86_INS_CMPSW: case X86_INS_CMPSD: case X86_INS_CMPSQ:
            case X86_INS_SCASB: case X86_INS_SCASW: case X86_INS_SCASD: case X86_INS_SCASQ:
            case X86_INS_STOSB: case X86_INS_STOSW: case X86_INS_STOSD: case X86_INS_STOSQ:
            case X86_INS_LODSB: case X86_INS_LODSW: case X86_INS_LODSD: case X86_INS_LODSQ:
                result.repeated = result.text.rfind("rep", 0) == 0; break;
            default: break;
        }
        std::ostringstream machine;
        for (unsigned i = 0; i < result.size; ++i) { if (i) machine << ' '; machine << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << static_cast<unsigned>(instruction->bytes[i]); }
        result.bytes = machine.str(); api.cs_free(instruction, 1); return result;
    }
};

inline CPU::CPU(Program assembled, const fs::path& runtimeDir) : impl_(std::make_unique<Impl>(runtimeDir)), program(std::move(assembled)) {
    if (program.base != MEMORY_BASE || program.machine_code.empty() || program.machine_code.size() > implementation::MAX_BINARY ||
        program.entry < program.base || program.entry >= program.base + program.machine_code.size()) throw DomainError("Programa x86 com origem, entrada ou tamanho invalido.");
    impl_->api.check(impl_->api.uc_mem_write(impl_->engine, program.base, program.machine_code.data(), program.machine_code.size()), "Falha ao carregar o binario x86");
    std::uint32_t stack = STACK_TOP, flags = 2;
    impl_->api.check(impl_->api.uc_reg_write(impl_->engine, UC_X86_REG_ESP, &stack), "Falha ao inicializar ESP");
    impl_->api.check(impl_->api.uc_reg_write(impl_->engine, UC_X86_REG_EIP, &program.entry), "Falha ao inicializar EIP");
    impl_->api.check(impl_->api.uc_reg_write(impl_->engine, UC_X86_REG_EFLAGS, &flags), "Falha ao inicializar EFLAGS");
}
inline CPU::~CPU() = default;
inline CPU::CPU(CPU&&) noexcept = default;
inline CPU& CPU::operator=(CPU&&) noexcept = default;
inline Snapshot CPU::snapshot() const {
    Snapshot result;
    for (std::size_t i = 0; i < result.regs.size(); ++i) impl_->api.check(impl_->api.uc_reg_read(impl_->engine, implementation::registerIds()[i], &result.regs[i]), "Falha ao ler registrador");
    impl_->api.check(impl_->api.uc_reg_read(impl_->engine, UC_X86_REG_EIP, &result.eip), "Falha ao ler EIP");
    impl_->api.check(impl_->api.uc_reg_read(impl_->engine, UC_X86_REG_EFLAGS, &result.eflags), "Falha ao ler EFLAGS");
    return result;
}
inline std::uint32_t CPU::pc() const { return snapshot().eip; }
inline std::vector<std::uint8_t> CPU::readMemory(std::uint32_t address, std::size_t size) const {
    if (!implementation::validRange(address, size)) throw DomainError("Leitura fora dos 2 MiB de memoria x86.");
    std::vector<std::uint8_t> result(size);
    if (size) impl_->api.check(impl_->api.uc_mem_read(impl_->engine, address, result.data(), size), "Falha ao ler a memoria x86");
    return result;
}
inline std::string CPU::nextInstruction() const { return halted ? "(parado)" : impl_->decode(pc()).text; }
inline TraceEntry CPU::step() {
    if (halted) throw DomainError("A CPU x86 esta parada. Monte ou reinicie o programa.");
    if (cycles >= 100000) throw DomainError("Limite de 100000 instrucoes no historico. Reinicie antes de continuar.");
    const auto before = snapshot();
    const auto instruction = impl_->decode(before.eip);
    uc_context* context = nullptr;
    impl_->api.check(impl_->api.uc_context_alloc(impl_->engine, &context), "Falha ao preparar o passo x86");
    struct ContextGuard { implementation::API& api; uc_context* context; ~ContextGuard() { api.uc_context_free(context); } } guard{impl_->api, context};
    impl_->api.check(impl_->api.uc_context_save(impl_->engine, context), "Falha ao salvar o estado x86");
    const auto outputBefore = output.size();
    const auto reasonBefore = halt_reason;
    impl_->oldMemory.clear(); impl_->writeLimit = false; impl_->tracking = true;
    try {
        if (instruction.id == X86_INS_HLT) {
            auto next = before.eip + instruction.size;
            impl_->api.check(impl_->api.uc_reg_write(impl_->engine, UC_X86_REG_EIP, &next), "Falha ao concluir HLT");
            halted = true; halt_reason = "HLT";
        } else if (instruction.id == X86_INS_INT) {
            const auto bytes = readMemory(before.eip, instruction.size);
            if (bytes.size() < 2 || bytes[bytes.size() - 1] != 0x80) throw DomainError("Interrupcao nao implementada. Apenas INT 0x80: sys_write e sys_exit.");
            const auto eax = before.regs[0], ebx = before.regs[1], ecx = before.regs[2], edx = before.regs[3];
            if (eax == 1) { halted = true; halt_reason = "sys_exit(" + std::to_string(ebx) + ")"; }
            else if (eax == 4) {
                std::uint32_t written = 0;
                if (ebx != 1 && ebx != 2) written = static_cast<std::uint32_t>(-9); // EBADF
                else if (edx == 0) written = 0;
                else if (!implementation::validRange(ecx, edx)) written = static_cast<std::uint32_t>(-14); // EFAULT
                else {
                    if (edx > implementation::MAX_OUTPUT - output.size()) throw DomainError("Limite de 64 KiB de saida atingido.");
                    const auto text = readMemory(ecx, edx); output.append(text.begin(), text.end()); written = edx;
                }
                impl_->api.check(impl_->api.uc_reg_write(impl_->engine, UC_X86_REG_EAX, &written), "Falha ao retornar sys_write");
            } else throw DomainError("INT 0x80: syscall " + std::to_string(eax) + " nao implementada. Disponiveis: 1 (exit) e 4 (write).");
            auto next = before.eip + instruction.size;
            impl_->api.check(impl_->api.uc_reg_write(impl_->engine, UC_X86_REG_EIP, &next), "Falha ao concluir INT 0x80");
        } else {
            switch (instruction.id) {
                case X86_INS_IN: case X86_INS_INSB: case X86_INS_INSD: case X86_INS_INSW:
                case X86_INS_OUT: case X86_INS_OUTSB: case X86_INS_OUTSD: case X86_INS_OUTSW:
                    throw DomainError("Entrada/saida por portas nao e implementada no ambiente didatico.");
                case X86_INS_INT1: case X86_INS_INT3: case X86_INS_INTO: case X86_INS_SYSCALL: case X86_INS_SYSENTER:
                    throw DomainError("Interrupcao ou chamada de sistema nao implementada. Use INT 0x80 (write/exit) ou HLT.");
                default: break;
            }
            // Unicorn counts each REP iteration when count=1. Run string instructions to their
            // fall-through address instead, so one ChipLab step represents one complete instruction.
            const auto error = impl_->api.uc_emu_start(impl_->engine, before.eip,
                instruction.repeated ? before.eip + instruction.size : 0, instruction.repeated ? 50000 : 0,
                instruction.repeated ? 0 : 1);
            impl_->api.check(error, "Erro de execucao x86 em " + implementation::hex(before.eip));
            std::size_t timedOut = 0;
            impl_->api.check(impl_->api.uc_query(impl_->engine, UC_QUERY_TIMEOUT, &timedOut), "Falha ao verificar o limite de execucao");
            if (timedOut) throw DomainError("A instrucao excedeu o limite de tempo e seu estado foi restaurado.");
            if (impl_->writeLimit) throw DomainError("A instrucao excedeu o limite de 256 KiB de escritas por passo.");
        }
        impl_->tracking = false;
        const auto after = snapshot();
        TraceEntry entry; entry.cycle = cycles + 1; entry.pc = before.eip; entry.next_pc = after.eip;
        entry.instruction = instruction.text; entry.bytes = instruction.bytes; entry.halted = halted;
        for (std::size_t i = 0; i < before.regs.size(); ++i) if (before.regs[i] != after.regs[i]) entry.register_changes[implementation::registerNames()[i]] = after.regs[i];
        if (before.eflags != after.eflags) entry.register_changes["EFLAGS"] = after.eflags;
        for (const auto& old : impl_->oldMemory) {
            std::uint8_t value = 0;
            impl_->api.check(impl_->api.uc_mem_read(impl_->engine, old.first, &value, 1), "Falha ao registrar a memoria");
            if (value != old.second) entry.memory_changes.emplace(old.first, value);
        }
        trace.push_back(entry); ++cycles; return entry;
    } catch (...) {
        impl_->tracking = false;
        for (const auto& old : impl_->oldMemory) impl_->api.uc_mem_write(impl_->engine, old.first, &old.second, 1);
        impl_->api.uc_context_restore(impl_->engine, context);
        output.resize(outputBefore); halted = false; halt_reason = reasonBefore;
        throw;
    }
}
inline void CPU::run(std::size_t steps) {
    if (!steps || steps > MAX_RUN_STEPS) throw DomainError("Executar: informe entre 1 e 1000 instrucoes.");
    for (std::size_t i = 0; i < steps && !halted; ++i) step();
}

}} // namespace chiplab::x86
#endif
