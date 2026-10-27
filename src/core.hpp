#ifndef CHIPLAB_CORE_HPP
#define CHIPLAB_CORE_HPP

// Motor didatico independente da interface. C++17 e APIs nativas para gravacao segura.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace chiplab {

constexpr std::size_t MAX_SOURCE_LENGTH = 100000;
constexpr std::size_t MAX_INSTRUCTIONS = 1024;
constexpr std::size_t MAX_PROJECT_BYTES = 1048576;
constexpr std::size_t MAX_RUN_STEPS = 1000;

class DomainError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

namespace detail {
inline std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
inline std::string upper(std::string value) {
    for (char& c : value) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return value;
}
inline bool identifier(const std::string& value) {
    if (value.empty()) return false;
    const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    if (!letter(value[0])) return false;
    return std::all_of(value.begin() + 1, value.end(), [&](char c) { return letter(c) || (c >= '0' && c <= '9'); });
}
inline bool register_token(const std::string& value) {
    if (value.size() < 2 || (value[0] != 'R' && value[0] != 'r')) return false;
    return std::all_of(value.begin() + 1, value.end(), [](char c) { return c >= '0' && c <= '9'; });
}
inline std::int64_t number(const std::string& value, const std::string& context) {
    const auto invalid = [&]() -> DomainError { return DomainError(context + ": inteiro decimal ou hexadecimal invalido: '" + value + "'."); };
    if (value.empty()) throw invalid();
    std::size_t pos = 0;
    bool negative = false;
    if (value[pos] == '+' || value[pos] == '-') { negative = value[pos] == '-'; ++pos; }
    int base = 10;
    if (value.size() > pos + 2 && value[pos] == '0' && (value[pos + 1] == 'x' || value[pos + 1] == 'X')) { base = 16; pos += 2; }
    if (pos == value.size()) throw invalid();
    std::uint64_t result = 0;
    const auto limit = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
    for (; pos < value.size(); ++pos) {
        const char c = value[pos];
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0 || digit >= base || result > (limit - static_cast<unsigned>(digit)) / static_cast<unsigned>(base)) throw invalid();
        result = result * static_cast<unsigned>(base) + static_cast<unsigned>(digit);
    }
    return negative ? -static_cast<std::int64_t>(result) : static_cast<std::int64_t>(result);
}
// Decodifica um ponto de codigo sem aceitar overlong, surrogate ou bytes incompletos.
inline std::uint32_t utf8_codepoint(const std::string& value, std::size_t& i) {
        if (i >= value.size()) throw DomainError("Texto UTF-8 incompleto.");
        const auto c = static_cast<unsigned char>(value[i++]);
        if (c < 0x80) return c;
        int continuation;
        std::uint32_t code;
        std::uint32_t minimum;
        if (c >= 0xC2 && c <= 0xDF) { continuation = 1; code = c & 0x1F; minimum = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { continuation = 2; code = c & 0x0F; minimum = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { continuation = 3; code = c & 0x07; minimum = 0x10000; }
        else throw DomainError("Texto invalido: utilize a codificacao UTF-8.");
        for (int j = 0; j < continuation; ++j) {
            if (i >= value.size()) throw DomainError("Texto UTF-8 incompleto.");
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xC0) != 0x80) throw DomainError("Texto UTF-8 invalido.");
            code = (code << 6) | (next & 0x3F);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) throw DomainError("Texto UTF-8 invalido.");
        return code;
}
// Valida UTF-8 e conta pontos de codigo; nao conta bytes como caracteres.
inline std::size_t utf8_length(const std::string& value) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < value.size(); ++count) (void)utf8_codepoint(value, i);
    return count;
}
inline bool unicode_whitespace(std::uint32_t code) {
    return (code >= 0x09 && code <= 0x0D) || code == 0x20 || code == 0x85 || code == 0xA0 ||
        code == 0x1680 || (code >= 0x2000 && code <= 0x200A) || code == 0x2028 || code == 0x2029 ||
        code == 0x202F || code == 0x205F || code == 0x3000;
}
inline void check_architecture_name(const std::string& name) {
    bool visible = false;
    std::size_t count = 0;
    for (std::size_t i = 0; i < name.size(); ++count) {
        const auto code = utf8_codepoint(name, i);
        if (code < 0x20 || (code >= 0x7F && code <= 0x9F))
            throw DomainError("O nome da arquitetura nao pode conter caracteres de controle.");
        visible = visible || !unicode_whitespace(code);
        if (count >= 60) throw DomainError("O nome da arquitetura deve conter de 1 a 60 caracteres.");
    }
    if (!visible) throw DomainError("O nome da arquitetura deve conter de 1 a 60 caracteres.");
}
inline void check_source(const std::string& source) {
    if (source.size() > MAX_SOURCE_LENGTH) throw DomainError("O codigo excede o limite de 100000 bytes em UTF-8.");
    (void)utf8_length(source);
    if (source.find('\0') != std::string::npos) throw DomainError("O codigo contem um caractere nulo invalido.");
}
} // namespace detail

struct Architecture {
    std::string name = "ChipLab";
    int bits = 8;
    int registers = 8;
    int memory_size = 64;

    void validate() const {
        detail::check_architecture_name(name);
        if (bits != 8 && bits != 16 && bits != 32) throw DomainError("Largura de dados: escolha 8, 16 ou 32 bits.");
        if (registers != 4 && registers != 8 && registers != 16) throw DomainError("Quantidade de registradores: escolha 4, 8 ou 16.");
        if (memory_size != 64 && memory_size != 128 && memory_size != 256) throw DomainError("Memoria: escolha 64, 128 ou 256 posicoes.");
    }
};

struct Instruction {
    std::string opcode;
    std::vector<std::int64_t> operands;
    std::size_t line = 0;
    std::string source;
    std::string text() const { return detail::trim(source); }
};

struct Program {
    std::vector<Instruction> instructions;
    std::map<std::string, std::size_t> labels;
    std::string source;
};

struct TraceEntry {
    std::uint64_t cycle = 0;
    std::size_t pc = 0;
    std::string instruction;
    std::size_t next_pc = 0;
    std::map<std::size_t, std::uint32_t> register_changes;
    std::map<std::size_t, std::uint32_t> memory_changes;
    bool halted = false;
};

inline Program assemble(const std::string& source, const Architecture& architecture) {
    architecture.validate();
    detail::check_source(source);
    Program result;
    result.source = source;
    struct Pending { std::size_t line; std::string original; std::string code; };
    std::vector<Pending> pending;
    std::istringstream lines(source);
    std::string original;
    std::size_t line = 0;
    while (std::getline(lines, original)) {
        ++line;
        // Editores comuns gravam UTF-8 com BOM. Apenas o marcador inicial e
        // ignorado na montagem; result.source e o limite em bytes o preservam.
        if (line == 1 && original.compare(0, 3, "\xEF\xBB\xBF") == 0) original.erase(0, 3);
        std::string code = detail::trim(original.substr(0, original.find_first_of("#;")));
        if (code.empty()) continue;
        const auto colon = code.find(':');
        if (colon != std::string::npos) {
            const auto label = detail::trim(code.substr(0, colon));
            if (!detail::identifier(label)) throw DomainError("Linha " + std::to_string(line) + ": rotulo invalido '" + label + "'.");
            if (!result.labels.emplace(label, pending.size()).second) throw DomainError("Linha " + std::to_string(line) + ": rotulo duplicado '" + label + "'.");
            code = detail::trim(code.substr(colon + 1));
            if (code.empty()) continue;
        }
        pending.push_back({line, original, code});
        if (pending.size() > MAX_INSTRUCTIONS) throw DomainError("O programa excede o limite de 1024 instrucoes.");
    }
    if (pending.empty()) throw DomainError("O programa esta vazio. Adicione instrucoes e termine com HALT.");
    for (const auto& item : pending) {
        const std::string context = "Linha " + std::to_string(item.line);
        const auto whitespace = item.code.find_first_of(" \t");
        std::string opcode = detail::upper(item.code.substr(0, whitespace));
        std::vector<std::string> tokens;
        if (whitespace != std::string::npos) {
            const auto tail = detail::trim(item.code.substr(whitespace));
            if (!tail.empty()) {
                std::size_t start = 0;
                while (true) {
                    const auto comma = tail.find(',', start);
                    tokens.push_back(detail::trim(tail.substr(start, comma == std::string::npos ? comma : comma - start)));
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            }
        }
        const auto count = [&](std::size_t expected) {
            if (tokens.size() != expected || std::any_of(tokens.begin(), tokens.end(), [](const std::string& s) { return s.empty(); }))
                throw DomainError(context + ": " + opcode + " exige " + std::to_string(expected) + " operando(s), separados por virgula.");
        };
        const auto reg = [&](const std::string& token) -> std::int64_t {
            if (!detail::register_token(token)) throw DomainError(context + ": registrador invalido '" + token + "'. Use R0 a R" + std::to_string(architecture.registers - 1) + ".");
            const auto value = detail::number(token.substr(1), context);
            if (value >= architecture.registers) throw DomainError(context + ": '" + token + "' nao existe. Use R0 a R" + std::to_string(architecture.registers - 1) + ".");
            return value;
        };
        const auto immediate = [&](const std::string& token) {
            const auto value = detail::number(token, context);
            const auto lower = -(std::int64_t{1} << (architecture.bits - 1));
            const auto upper = (std::int64_t{1} << architecture.bits) - 1;
            if (value < lower || value > upper) throw DomainError(context + ": imediato fora da faixa " + std::to_string(lower) + " a " + std::to_string(upper) + ".");
            return value;
        };
        const auto address = [&](const std::string& token) {
            const auto value = detail::number(token, context);
            if (value < 0 || value >= architecture.memory_size) throw DomainError(context + ": endereco fora da memoria (0 a " + std::to_string(architecture.memory_size - 1) + ").");
            return value;
        };
        const auto target = [&](const std::string& token) -> std::int64_t {
            std::int64_t value;
            const auto found = result.labels.find(token);
            if (found != result.labels.end()) value = static_cast<std::int64_t>(found->second);
            else if (detail::identifier(token)) throw DomainError(context + ": rotulo desconhecido '" + token + "'.");
            else value = detail::number(token, context);
            if (value < 0 || static_cast<std::size_t>(value) >= pending.size()) throw DomainError(context + ": destino de salto fora do programa.");
            return value;
        };
        std::vector<std::int64_t> operands;
        if (opcode == "MOV") {
            count(2);
            operands.push_back(reg(tokens[0]));
            if (detail::register_token(tokens[1])) { operands.push_back(reg(tokens[1])); opcode = "MOVR"; }
            else operands.push_back(immediate(tokens[1]));
        } else if (opcode == "ADD" || opcode == "SUB" || opcode == "AND" || opcode == "OR" || opcode == "XOR") {
            count(3);
            operands = {reg(tokens[0]), reg(tokens[1]), reg(tokens[2])};
        } else if (opcode == "ADDI") {
            count(3);
            operands = {reg(tokens[0]), reg(tokens[1]), immediate(tokens[2])};
        } else if (opcode == "LOAD" || opcode == "STORE") {
            count(2);
            operands = {reg(tokens[0]), address(tokens[1])};
        } else if (opcode == "JMP") {
            count(1);
            operands = {target(tokens[0])};
        } else if (opcode == "JZ" || opcode == "JNZ") {
            count(2);
            operands = {reg(tokens[0]), target(tokens[1])};
        } else if (opcode == "HALT") count(0);
        else throw DomainError(context + ": instrucao desconhecida '" + opcode + "'.");
        result.instructions.push_back({opcode, operands, item.line, item.original});
    }
    return result;
}

class CPU {
public:
    Architecture architecture;
    Program program;
    std::vector<std::uint32_t> registers;
    std::vector<std::uint32_t> memory;
    std::size_t pc = 0;
    std::uint64_t cycles = 0;
    bool halted = false;
    std::vector<TraceEntry> trace;

    CPU(const Architecture& config, const Program& assembled)
        : architecture(config), program(assemble(assembled.source, config)),
          registers(static_cast<std::size_t>(config.registers), 0), memory(static_cast<std::size_t>(config.memory_size), 0) {}

    TraceEntry step() {
        if (halted) throw DomainError("O processador ja foi encerrado por HALT. Reinicie para executar novamente.");
        architecture.validate();
        const auto inconsistent = [] { throw DomainError("Estado interno inconsistente. Monte o programa novamente."); };
        if (registers.size() != static_cast<std::size_t>(architecture.registers) ||
            memory.size() != static_cast<std::size_t>(architecture.memory_size)) inconsistent();
        if (pc >= program.instructions.size()) throw DomainError("O contador de programa saiu do codigo. Finalize o programa com HALT.");
        const auto& instruction = program.instructions[pc];
        const auto& opcode = instruction.opcode;
        const auto& args = instruction.operands;
        // A API C++ expõe o estado para inspecao. Se um cliente o alterar,
        // rejeita operandos invalidos antes de gravar PC, ciclos ou rastro.
        const auto count = [&](std::size_t expected) { if (args.size() != expected) inconsistent(); };
        const auto reg = [&](std::size_t index) { if (args[index] < 0 || static_cast<std::size_t>(args[index]) >= registers.size()) inconsistent(); };
        const auto immediate = [&](std::size_t index) {
            const auto lower = -(std::int64_t{1} << (architecture.bits - 1));
            const auto upper = (std::int64_t{1} << architecture.bits) - 1;
            if (args[index] < lower || args[index] > upper) inconsistent();
        };
        const auto address = [&](std::size_t index) { if (args[index] < 0 || static_cast<std::size_t>(args[index]) >= memory.size()) inconsistent(); };
        const auto target = [&](std::size_t index) { if (args[index] < 0 || static_cast<std::size_t>(args[index]) >= program.instructions.size()) inconsistent(); };
        if (opcode == "MOV") { count(2); reg(0); immediate(1); }
        else if (opcode == "MOVR") { count(2); reg(0); reg(1); }
        else if (opcode == "ADD" || opcode == "SUB" || opcode == "AND" || opcode == "OR" || opcode == "XOR") { count(3); reg(0); reg(1); reg(2); }
        else if (opcode == "ADDI") { count(3); reg(0); reg(1); immediate(2); }
        else if (opcode == "LOAD" || opcode == "STORE") { count(2); reg(0); address(1); }
        else if (opcode == "JMP") { count(1); target(0); }
        else if (opcode == "JZ" || opcode == "JNZ") { count(2); reg(0); target(1); }
        else if (opcode == "HALT") count(0);
        else inconsistent();
        const auto mask = (std::uint64_t{1} << architecture.bits) - 1;
        TraceEntry entry;
        entry.cycle = cycles + 1;
        entry.pc = pc;
        entry.instruction = instruction.text();
        entry.next_pc = pc + 1;
        const auto read = [&](std::size_t operand) { return registers.at(static_cast<std::size_t>(args.at(operand))); };
        const auto write = [&](std::uint64_t value) { entry.register_changes[static_cast<std::size_t>(args.at(0))] = static_cast<std::uint32_t>(value & mask); };
        try {
            if (opcode == "MOV") write(static_cast<std::uint64_t>(args.at(1)));
            else if (opcode == "MOVR") write(read(1));
            else if (opcode == "ADD") write(static_cast<std::uint64_t>(read(1)) + read(2));
            else if (opcode == "SUB") write(static_cast<std::uint64_t>(read(1)) - read(2));
            else if (opcode == "AND") write(read(1) & read(2));
            else if (opcode == "OR") write(read(1) | read(2));
            else if (opcode == "XOR") write(read(1) ^ read(2));
            else if (opcode == "ADDI") write(static_cast<std::uint64_t>(read(1)) + static_cast<std::uint64_t>(args.at(2)));
            else if (opcode == "LOAD") write(memory.at(static_cast<std::size_t>(args.at(1))));
            else if (opcode == "STORE") entry.memory_changes[static_cast<std::size_t>(args.at(1))] = read(0);
            else if (opcode == "JMP") entry.next_pc = static_cast<std::size_t>(args.at(0));
            else if (opcode == "JZ") { if (read(0) == 0) entry.next_pc = static_cast<std::size_t>(args.at(1)); }
            else if (opcode == "JNZ") { if (read(0) != 0) entry.next_pc = static_cast<std::size_t>(args.at(1)); }
            else if (opcode == "HALT") entry.halted = true;
            else throw DomainError("Instrucao interna invalida: " + opcode + ".");
            // Verifica destinos antes de qualquer mutacao (passo transacional).
            for (const auto& change : entry.register_changes) (void)registers.at(change.first);
            for (const auto& change : entry.memory_changes) (void)memory.at(change.first);
        } catch (const std::out_of_range&) {
            throw DomainError("Estado interno inconsistente. Monte o programa novamente.");
        }
        trace.push_back(entry); // alocacao antes de alterar o estado da CPU
        for (const auto& change : entry.register_changes) registers[change.first] = change.second;
        for (const auto& change : entry.memory_changes) memory[change.first] = change.second;
        pc = entry.next_pc;
        cycles = entry.cycle;
        halted = entry.halted;
        return entry;
    }

    std::vector<TraceEntry> run(std::size_t max_steps = 1000) {
        if (max_steps == 0 || max_steps > MAX_RUN_STEPS) throw DomainError("O limite deve estar entre 1 e 1000 passos.");
        std::vector<TraceEntry> result;
        for (std::size_t i = 0; i < max_steps && !halted; ++i) result.push_back(step());
        if (!halted) throw DomainError("Limite de " + std::to_string(max_steps) + " passos atingido. Verifique lacos ou continue a execucao.");
        return result;
    }
};

namespace detail {
inline std::string json_string(const std::string& value) {
    static const char* hex = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
                else out += static_cast<char>(c);
        }
    }
    return out + '"';
}
// Parser restrito aos tipos previstos no esquema: objeto, string e inteiro.
// Booleanos, null, arrays, floats, chaves repetidas e campos extras sao rejeitados.
struct JsonValue {
    enum class Type { Object, String, Integer } type = Type::Object;
    std::map<std::string, JsonValue> object;
    std::string string;
    std::int64_t integer = 0;
};
class JsonParser {
    const std::string& text_;
    std::size_t pos_ = 0;
    [[noreturn]] void fail() const { throw DomainError("Projeto invalido: JSON malformado ou tipo de campo nao permitido."); }
    void whitespace() { while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\r' || text_[pos_] == '\n')) ++pos_; }
    bool consume(char token) { whitespace(); if (pos_ < text_.size() && text_[pos_] == token) { ++pos_; return true; } return false; }
    std::uint32_t hex4() {
        std::uint32_t code = 0;
        for (int n = 0; n < 4; ++n) {
            if (pos_ >= text_.size()) fail();
            char c = text_[pos_++];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) fail();
            code = (code << 4) | static_cast<unsigned>(d);
        }
        return code;
    }
    void utf8(std::string& out, std::uint32_t code) {
        if (code < 0x80) out += static_cast<char>(code);
        else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 63)); }
        else if (code < 0x10000) { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 63)); out += static_cast<char>(0x80 | (code & 63)); }
        else { out += static_cast<char>(0xF0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 63)); out += static_cast<char>(0x80 | ((code >> 6) & 63)); out += static_cast<char>(0x80 | (code & 63)); }
    }
    std::string string() {
        if (!consume('"')) fail();
        std::string out;
        while (pos_ < text_.size()) {
            unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') { utf8_length(out); return out; }
            if (c < 0x20) fail();
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (pos_ >= text_.size()) fail();
            char escaped = text_[pos_++];
            switch (escaped) {
                case '"': case '\\': case '/': out += escaped; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    auto code = hex4();
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (pos_ + 2 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') fail();
                        pos_ += 2;
                        const auto low = hex4();
                        if (low < 0xDC00 || low > 0xDFFF) fail();
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code >= 0xDC00 && code <= 0xDFFF) fail();
                    utf8(out, code);
                    break;
                }
                default: fail();
            }
        }
        fail();
    }
    JsonValue value(unsigned depth) {
        if (depth > 8) fail();
        whitespace();
        if (pos_ >= text_.size()) fail();
        JsonValue out;
        if (text_[pos_] == '{') {
            ++pos_;
            if (consume('}')) return out;
            do {
                const auto key = string();
                if (!consume(':')) fail();
                if (!out.object.emplace(key, value(depth + 1)).second) throw DomainError("Projeto invalido: campo duplicado '" + key + "'.");
                if (consume('}')) return out;
            } while (consume(','));
            fail();
        }
        if (text_[pos_] == '"') { out.type = JsonValue::Type::String; out.string = string(); return out; }
        const auto start = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') fail();
        if (text_[pos_] == '0') ++pos_;
        else while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        out.type = JsonValue::Type::Integer;
        out.integer = number(text_.substr(start, pos_ - start), "Projeto");
        return out;
    }
public:
    // Editores de texto Windows podem prefixar UTF-8 com um unico BOM.
    // Somente o marcador inicial e ignorado; dentro do documento segue invalido.
    explicit JsonParser(const std::string& text) : text_(text) {
        if (text_.size() >= 3 && text_.compare(0, 3, "\xEF\xBB\xBF") == 0) pos_ = 3;
    }
    JsonValue parse() { auto out = value(0); whitespace(); if (pos_ != text_.size()) fail(); return out; }
};

inline void keys(const JsonValue& value, std::initializer_list<const char*> expected) {
    if (value.type != JsonValue::Type::Object || value.object.size() != expected.size()) throw DomainError("Projeto invalido: campos ausentes ou desconhecidos.");
    for (const char* key : expected) if (value.object.find(key) == value.object.end()) throw DomainError("Projeto invalido: campo ausente '" + std::string(key) + "'.");
}
inline const std::string& string_field(const JsonValue& object, const char* key) {
    const auto& value = object.object.at(key);
    if (value.type != JsonValue::Type::String) throw DomainError("Projeto invalido: '" + std::string(key) + "' deve ser texto.");
    return value.string;
}
inline int int_field(const JsonValue& object, const char* key) {
    const auto& value = object.object.at(key);
    if (value.type != JsonValue::Type::Integer || value.integer < (std::numeric_limits<int>::min)() || value.integer > (std::numeric_limits<int>::max)()) throw DomainError("Projeto invalido: '" + std::string(key) + "' deve ser inteiro.");
    return static_cast<int>(value.integer);
}
// Compartilhado por projetos e exportacoes para evitar truncar o destino
// quando a escrita, sincronizacao ou substituicao falhar.
inline void write_file_atomic(const std::filesystem::path& path, const std::string& payload) {
    // O arquivo anterior so e substituido depois de o temporario estar completo.
    // A criacao exclusiva evita sobrescrever arquivos de outra chamada/processo.
    struct Temporary {
        std::filesystem::path path;
        bool owned = false;
        ~Temporary() {
            if (owned) { std::error_code ignored; std::filesystem::remove(path, ignored); }
        }
    } temporary;
    static std::atomic<std::uint64_t> sequence{0};
    const auto nonce = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        temporary.path = path;
        temporary.path += ".chiplab-" + std::to_string(nonce) + "-" + std::to_string(sequence.fetch_add(1)) + ".tmp";
#ifdef _WIN32
        handle = CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (handle != INVALID_HANDLE_VALUE) { temporary.owned = true; break; }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            throw DomainError("Nao foi possivel criar o arquivo temporario.");
#else
        handle = ::open(temporary.path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (handle >= 0) { temporary.owned = true; break; }
        if (errno != EEXIST) throw DomainError("Nao foi possivel criar o arquivo temporario.");
#endif
    }
    if (!temporary.owned) throw DomainError("Nao foi possivel reservar um arquivo temporario unico.");
    bool complete = true;
    std::size_t offset = 0;
    while (offset < payload.size()) {
        const auto chunk = (std::min)(payload.size() - offset, std::size_t{1048576});
#ifdef _WIN32
        DWORD written = 0;
        if (!WriteFile(handle, payload.data() + offset, static_cast<DWORD>(chunk), &written, nullptr) || written == 0) { complete = false; break; }
        offset += written;
#else
        const auto written = ::write(handle, payload.data() + offset, chunk);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { complete = false; break; }
        offset += static_cast<std::size_t>(written);
#endif
    }
#ifdef _WIN32
    if (complete && !FlushFileBuffers(handle)) complete = false;
    if (!CloseHandle(handle)) complete = false;
#else
    if (complete && ::fsync(handle) != 0) complete = false;
    if (::close(handle) != 0) complete = false;
#endif
    if (!complete) throw DomainError("Nao foi possivel gravar o arquivo completo. O arquivo anterior foi preservado.");
#ifdef _WIN32
    if (!MoveFileExW(temporary.path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw DomainError("Nao foi possivel substituir o arquivo. O arquivo anterior foi preservado.");
#else
    std::error_code error;
    std::filesystem::rename(temporary.path, path, error);
    if (error) throw DomainError("Nao foi possivel substituir o arquivo. O arquivo anterior foi preservado.");
#endif
    temporary.owned = false;
}
} // namespace detail

inline void save_project(const std::filesystem::path& path, const Architecture& architecture, const std::string& source) {
    architecture.validate();
    detail::check_source(source); // rascunhos incompletos tambem podem ser salvos
    const auto payload = std::string("{\n  \"version\": 1,\n  \"architecture\": {\n    \"name\": ") + detail::json_string(architecture.name) +
        ",\n    \"bits\": " + std::to_string(architecture.bits) + ",\n    \"registers\": " + std::to_string(architecture.registers) +
        ",\n    \"memory_size\": " + std::to_string(architecture.memory_size) + "\n  },\n  \"source\": " + detail::json_string(source) + "\n}\n";
    if (payload.size() > MAX_PROJECT_BYTES) throw DomainError("O projeto excede o limite de 1 MiB.");
    detail::write_file_atomic(path, payload);
}

inline std::pair<Architecture, std::string> load_project(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw DomainError("Nao foi possivel abrir o arquivo do projeto.");
    std::string payload(MAX_PROJECT_BYTES + 1, '\0');
    input.read(&payload[0], static_cast<std::streamsize>(payload.size()));
    if (input.bad()) throw DomainError("Falha ao ler o arquivo do projeto.");
    payload.resize(static_cast<std::size_t>(input.gcount()));
    if (payload.size() > MAX_PROJECT_BYTES) throw DomainError("O projeto excede o limite de 1 MiB.");
    const auto document = detail::JsonParser(payload).parse();
    detail::keys(document, {"version", "architecture", "source"});
    if (detail::int_field(document, "version") != 1) throw DomainError("Versao de projeto incompativel. Este sistema utiliza a versao 1.");
    const auto& config = document.object.at("architecture");
    detail::keys(config, {"name", "bits", "registers", "memory_size"});
    Architecture architecture{detail::string_field(config, "name"), detail::int_field(config, "bits"), detail::int_field(config, "registers"), detail::int_field(config, "memory_size")};
    architecture.validate();
    auto source = detail::string_field(document, "source");
    detail::check_source(source);
    return {architecture, source};
}

inline std::string export_verilog(const Architecture& architecture) {
    architecture.validate();
    return std::string("// ChipLab: ULA combinacional didatica, nao uma CPU completa.\n") +
        "// Resultado sem sinal. ADD e SUB descartam carry/borrow (modulo 2**WIDTH).\n" +
        "// op: 000=ADD, 001=SUB, 010=AND, 011=OR, 100=XOR; demais: zero.\n" +
        "module chiplab_alu #(\n    parameter WIDTH = " + std::to_string(architecture.bits) +
        "\n)(\n    input wire [WIDTH-1:0] a,\n    input wire [WIDTH-1:0] b,\n    input wire [2:0] op,\n" +
        "    output reg [WIDTH-1:0] result,\n    output wire zero\n);\n    always @* begin\n        case (op)\n" +
        "            3'b000: result = a + b;\n            3'b001: result = a - b;\n" +
        "            3'b010: result = a & b;\n            3'b011: result = a | b;\n" +
        "            3'b100: result = a ^ b;\n            default: result = {WIDTH{1'b0}};\n" +
        "        endcase\n    end\n    assign zero = (result == {WIDTH{1'b0}});\nendmodule\n";
}

} // namespace chiplab
#endif
