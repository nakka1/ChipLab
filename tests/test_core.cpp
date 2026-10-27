#include "../src/core.hpp"
#include <chrono>
#include <functional>
#include <iostream>
#include <iterator>

using namespace chiplab;

static int checks = 0;
static void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error("Falhou: " + message);
}
static void rejects(const std::function<void()>& task, const std::string& message) {
    try { task(); } catch (const DomainError&) { ++checks; return; }
    throw std::runtime_error("Deveria rejeitar: " + message);
}
static CPU machine(const std::string& source, Architecture a = {}) { return CPU(a, assemble(source, a)); }

static void test_arithmetic_widths() {
    for (int bits : {8, 16, 32}) {
        Architecture a{"Teste", bits, 8, 64};
        const auto maximum = (std::uint64_t{1} << bits) - 1;
        auto cpu = machine("MOV R0, " + std::to_string(maximum) + "\nMOV R1, 1\nADD R2, R0, R1\nSUB R3, R2, R1\nHALT", a);
        cpu.run();
        check(cpu.registers[2] == 0 && cpu.registers[3] == maximum, "overflow e underflow em " + std::to_string(bits) + " bits");
        check(cpu.cycles == 5 && cpu.halted, "HALT custa um ciclo");
    }
}
static void test_logic_and_immediates() {
    auto cpu = machine("mov r0, 0xF0\nMOV R1, 0xCC\nAND R2, R0, R1\nOR R3, R0, R1\nXOR R4, R0, R1\nMOV R5, R4\nADDI R6, R5, -0x40\nMOV R7, -128\nHALT");
    cpu.run();
    check(cpu.registers == std::vector<std::uint32_t>({240, 204, 192, 252, 60, 60, 252, 128}), "logica, MOV de registrador e imediatos assinados/hex");
}
static void test_labels_branches_and_memory() {
    auto cpu = machine("# Soma 1+2+3\nMOV R0, 3\nMOV R1, 0\nJZ R0, done\nloop: ADD R1, R1, R0\nADDI R0, R0, -1\nJNZ R0, loop\nJNZ R0, loop\nJZ R0, done\nMOV R1, 99\ndone: STORE R1, 63\nLOAD R2, 63\nJMP finish\nMOV R2, 99\nfinish: HALT\n");
    const auto trace = cpu.run();
    check(cpu.registers[0] == 0 && cpu.registers[1] == 6 && cpu.registers[2] == 6 && cpu.memory[63] == 6, "ramos tomados/nao tomados, saltos e memoria");
    check(cpu.cycles == 18, "ciclos do laco");
    check(cpu.program.instructions[0].line == 2 && cpu.program.labels.at("loop") == 3, "duas passagens e linhas fonte");
    check(trace.front().cycle == 1 && trace.back().next_pc == cpu.program.instructions.size(), "rastreamento e PC apos HALT");
    auto direct = machine("JMP 2\nMOV R0, 99\nHALT");
    direct.run();
    check(direct.registers[0] == 0, "salto por indice numerico");
}
static void test_bounded_execution_and_resume() {
    auto cpu = machine("MOV R0, 4\nloop: ADDI R0, R0, -1\nJNZ R0, loop\nHALT");
    rejects([&] { cpu.run(3); }, "limite de passos");
    check(cpu.cycles == 3 && cpu.registers[0] == 3 && !cpu.halted, "limite preserva progresso");
    cpu.run(10);
    check(cpu.halted && cpu.registers[0] == 0, "retomada apos limite");
    auto loop = machine("forever: JMP forever");
    rejects([&] { loop.run(7); }, "laco infinito");
    check(loop.cycles == 7 && loop.trace.size() == 7 && loop.pc == 0, "laco limitado");
    rejects([&] { loop.run(0); }, "limite zero");
    rejects([&] { loop.run(MAX_RUN_STEPS + 1); }, "limite excessivo");
}
static void test_transactional_runtime_errors() {
    auto cpu = machine("MOV R0, 8");
    cpu.step();
    const auto pc = cpu.pc;
    const auto cycles = cpu.cycles;
    const auto regs = cpu.registers;
    const auto mem = cpu.memory;
    const auto trace_size = cpu.trace.size();
    rejects([&] { cpu.step(); }, "fim sem HALT");
    check(cpu.pc == pc && cpu.cycles == cycles && cpu.registers == regs && cpu.memory == mem && cpu.trace.size() == trace_size, "erro nao altera estado");
    cpu = machine("HALT");
    cpu.step();
    rejects([&] { cpu.step(); }, "passo apos HALT");
    check(cpu.cycles == 1, "HALT nao executa novamente");
    cpu = machine("STORE R0, 0\nHALT");
    cpu.program.instructions[0].operands[1] = 999;
    rejects([&] { cpu.step(); }, "estado interno corrompido");
    check(cpu.cycles == 0 && cpu.trace.empty() && cpu.memory[0] == 0, "operando invalido e transacional");
}
static void test_assembler_rejections_and_limits() {
    const std::vector<std::string> invalid = {"MOV R8, 1", "MOV R0, 256", "MOV R0, -129", "ADDI R0, R1, 256", "LOAD R0, 64", "STORE R0, -1", "JMP absent", "JMP 99", "HALT R0", "ADD R0, R1", "MOV R0,, 1", "MOV R0 R1", "MOV R0, 1,", "NOPE R0", "dup: HALT\ndup: HALT", "9label: HALT", "JMP end\nend:", "JMP End\nend: HALT", "MOV R0, 99999999999999999999999999999999999999999999"};
    for (const auto& source : invalid) rejects([&] { (void)assemble(source, {}); }, source);
    rejects([] { (void)assemble("# vazio", {}); }, "programa vazio");
    rejects([] { (void)assemble(std::string(MAX_SOURCE_LENGTH + 1, 'x'), {}); }, "fonte muito grande");
    rejects([] { (void)assemble(std::string("HALT\0", 5), {}); }, "NUL");
    rejects([] { (void)assemble(std::string("HALT\n\xFF", 6), {}); }, "UTF-8 invalido");
    std::string many;
    for (std::size_t i = 0; i < MAX_INSTRUCTIONS; ++i) many += "HALT\n";
    check(assemble(many, {}).instructions.size() == MAX_INSTRUCTIONS, "limite exato de instrucoes");
    rejects([&] { (void)assemble(many + "HALT", {}); }, "instrucoes excedentes");
    Architecture config;
    config.bits = 7; rejects([&] { config.validate(); }, "bits");
    config = {}; config.registers = 5; rejects([&] { config.validate(); }, "registradores");
    config = {}; config.memory_size = 32; rejects([&] { config.validate(); }, "memoria");
    config = {}; config.name = " "; rejects([&] { config.validate(); }, "nome vazio");
    config.name = std::string(61, 'x'); rejects([&] { config.validate(); }, "nome longo");
    auto wide_program = assemble("MOV R7, 1\nHALT", {});
    rejects([&] { CPU wrong(Architecture{"Quatro", 8, 4, 64}, wide_program); }, "programa para outra arquitetura");
}
static void write_file(const std::filesystem::path& path, const std::string& value) { std::ofstream out(path, std::ios::binary); out.write(value.data(), static_cast<std::streamsize>(value.size())); if (!out) throw std::runtime_error("Falha no arquivo temporario de teste"); }
static void test_project_roundtrip_and_strict_schema() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto folder = std::filesystem::current_path() / (".test-chiplab-" + std::to_string(unique));
    std::filesystem::create_directory(folder);
    const auto file = folder / "projeto.json";
    try {
        const Architecture config{"Processador de teste", 16, 4, 128};
        const std::string source = "; rascunho com acentuacao, aspas \" e barra \\\nMOV R0,";
        save_project(file, config, source);
        const auto loaded = load_project(file);
        check(loaded.first.name == config.name && loaded.first.bits == 16 && loaded.first.registers == 4 && loaded.first.memory_size == 128 && loaded.second == source, "roundtrip preserva configuracao e rascunho");
        const std::string prefix = "{\"version\":1,\"architecture\":{\"name\":\"Teste\",\"bits\":8,\"registers\":8,\"memory_size\":64},\"source\":";
        const std::vector<std::string> invalid = {"", "{", "[]", "{}", "{\"version\":1,\"version\":1}", prefix + "true}", prefix + "null}", prefix + "[]}", prefix + "3}", prefix + "\"HALT\",\"extra\":0}", prefix + "\"HALT\"} trailing", prefix + "\"HALT\",}", prefix + "\"\\uD800\"}", prefix + "\"\\uDC00\"}", prefix + "\"\\uD800\\u0041\"}", prefix + "\"\\x\"}", prefix + "\"HALT\n\"}", std::string(MAX_PROJECT_BYTES + 1, ' '), prefix + detail::json_string(std::string(MAX_SOURCE_LENGTH + 1, 'x')) + "}", "{\"version\":true,\"architecture\":{},\"source\":\"HALT\"}", "{\"version\":1.0,\"architecture\":{},\"source\":\"HALT\"}", "{\"version\":01,\"architecture\":{},\"source\":\"HALT\"}", "{\"version\":2,\"architecture\":{},\"source\":\"HALT\"}", "{\"version\":1,\"architecture\":{\"name\":\"a\",\"bits\":64,\"registers\":8,\"memory_size\":64},\"source\":\"HALT\"}"};
        for (const auto& payload : invalid) { write_file(file, payload); rejects([&] { (void)load_project(file); }, "JSON invalido: " + payload.substr(0, 50)); }
        write_file(file, prefix + "\"; \\u00e1 \\ud83d\\ude80\nHALT\"}");
        // Raw newline in JSON strings is forbidden; escaped newline is accepted.
        rejects([&] { (void)load_project(file); }, "newline bruto no JSON");
        write_file(file, prefix + "\"; \\u00e1 \\ud83d\\ude80\\nHALT\"}");
        check(load_project(file).second == "; \xc3\xa1 \xf0\x9f\x9a\x80\nHALT", "escapes Unicode e surrogate pair");
        rejects([&] { (void)load_project(folder / "ausente.json"); }, "arquivo ausente");
        rejects([&] { save_project(folder / "ausente" / "a.json", {}, "HALT"); }, "diretorio ausente");
    } catch (...) {
        std::filesystem::remove(file);
        std::filesystem::remove(folder);
        throw;
    }
    std::filesystem::remove(file);
    std::filesystem::remove(folder);
}
static void test_safe_save_preserves_existing_and_cleans_temporary() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto folder = std::filesystem::current_path() / (".test-safe-save-" + std::to_string(unique));
    std::filesystem::create_directory(folder);
    const auto target = folder / "existing.json";
    const auto blocked = folder / "directory.json";
    std::filesystem::create_directory(blocked);
    const auto read_bytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const auto no_temporary = [&] {
        for (const auto& entry : std::filesystem::directory_iterator(folder))
            if (entry.path().filename().string().find(".chiplab-") != std::string::npos) return false;
        return true;
    };
    try {
        write_file(target, "conteudo anterior exato");
        const auto previous = read_bytes(target);
        rejects([&] { save_project(target, {}, std::string(MAX_SOURCE_LENGTH + 1, 'x')); }, "fonte invalida preserva arquivo existente");
        check(read_bytes(target) == previous && no_temporary(), "validacao ocorre antes de criar temporario");
        Architecture invalid;
        invalid.bits = 7;
        rejects([&] { save_project(target, invalid, "HALT"); }, "arquitetura invalida preserva arquivo existente");
        check(read_bytes(target) == previous && no_temporary(), "arquitetura invalida nao altera arquivo");
        save_project(target, {"Substituido", 32, 16, 256}, "MOV R0, 4294967295\nHALT");
        const auto loaded = load_project(target);
        check(loaded.first.name == "Substituido" && loaded.first.bits == 32 && loaded.second == "MOV R0, 4294967295\nHALT", "substituicao valida atomica");
        check(no_temporary(), "substituicao bem sucedida nao deixa temporario");
        const auto valid_bytes = read_bytes(target);
#ifdef _WIN32
        // Simula destino aberto sem compartilhamento de exclusao: a gravacao do
        // temporario termina, mas a substituicao do arquivo original deve falhar.
        HANDLE locked = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "fixture bloqueia substituicao do arquivo");
        try { rejects([&] { save_project(target, {}, "HALT"); }, "destino bloqueado preserva original"); }
        catch (...) { CloseHandle(locked); throw; }
        CloseHandle(locked);
        check(read_bytes(target) == valid_bytes && no_temporary(), "falha depois da gravacao preserva original e limpa temporario");
#endif
        rejects([&] { save_project(blocked, {}, "HALT"); }, "destino e diretorio");
        check(std::filesystem::is_directory(blocked) && read_bytes(target) == valid_bytes && no_temporary(), "diretorio preservado e temporario removido apos falha");
    } catch (...) {
        std::filesystem::remove(target);
        std::filesystem::remove(blocked);
        std::filesystem::remove(folder);
        throw;
    }
    std::filesystem::remove(target);
    std::filesystem::remove(blocked);
    std::filesystem::remove(folder);
}
static void test_verilog_export() {
    const auto code = export_verilog({"name; injected", 32, 8, 64});
    check(code.find("module chiplab_alu") != std::string::npos && code.find("parameter WIDTH = 32") != std::string::npos, "modulo fixo e largura");
    check(code.find("injected") == std::string::npos, "nome nao e interpolado no HDL");
    for (const auto* op : {"a + b", "a - b", "a & b", "a | b", "a ^ b", "default: result = {WIDTH{1'b0}};"}) check(code.find(op) != std::string::npos, op);
}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"test_arithmetic_widths", test_arithmetic_widths},
        {"test_logic_and_immediates", test_logic_and_immediates},
        {"test_labels_branches_and_memory", test_labels_branches_and_memory},
        {"test_bounded_execution_and_resume", test_bounded_execution_and_resume},
        {"test_transactional_runtime_errors", test_transactional_runtime_errors},
        {"test_assembler_rejections_and_limits", test_assembler_rejections_and_limits},
        {"test_project_roundtrip_and_strict_schema", test_project_roundtrip_and_strict_schema},
        {"test_safe_save_preserves_existing_and_cleans_temporary", test_safe_save_preserves_existing_and_cleans_temporary},
        {"test_verilog_export", test_verilog_export}
    };
    try { for (const auto& test : tests) { test.second(); std::cout << "PASS " << test.first << '\n'; } }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << tests.size() << " grupos, " << checks << " verificacoes: OK\n";
    return 0;
}
