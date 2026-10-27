#include "../src/core.hpp"
#include <array>
#include <functional>
#include <iostream>
#include <random>

using namespace chiplab;

namespace {
std::size_t scenarios = 0;
std::size_t assertions = 0;
std::size_t differential_steps = 0;

void require(bool condition, const std::string& context) {
    ++assertions;
    if (!condition) throw std::runtime_error(context);
}
void scenario() { ++scenarios; }
void reject(const std::function<void()>& action, const std::string& context) {
    bool rejected = false;
    try { action(); } catch (const DomainError&) { rejected = true; }
    require(rejected, context + ": expected DomainError");
}
CPU make_cpu(const std::string& source, Architecture config = {}) {
    return CPU(config, assemble(source, config));
}
std::uint64_t word_mask(int bits) { return (std::uint64_t{1} << bits) - 1; }

void architecture_boundaries() {
    for (int bits : {8, 16, 32}) for (int registers : {4, 8, 16}) for (int memory : {64, 128, 256}) {
        scenario();
        const Architecture config{"Boundary", bits, registers, memory};
        const auto maximum = word_mask(bits);
        const auto minimum = -(std::int64_t{1} << (bits - 1));
        const auto last = "R" + std::to_string(registers - 1);
        const auto address = std::to_string(memory - 1);
        auto cpu = make_cpu("MOV R0, " + std::to_string(maximum) + "\nMOV R1, 1\nADD R2, R0, R1\nSUB R3, R2, R1\nMOV " + last + ", " + std::to_string(minimum) + "\nSTORE " + last + ", " + address + "\nLOAD R0, " + address + "\nHALT", config);
        require(cpu.registers.size() == static_cast<std::size_t>(registers) && cpu.memory.size() == static_cast<std::size_t>(memory), "Architecture allocation");
        cpu.run();
        require(cpu.registers[2] == 0 && cpu.registers[0] == (std::uint64_t{1} << (bits - 1)) && cpu.memory.back() == cpu.registers[0] && cpu.pc == 8 && cpu.cycles == 8 && cpu.halted, "Architecture boundary execution");
        for (const auto& bad : {
                 "MOV R0, " + std::to_string(maximum + 1),
                 "MOV R0, " + std::to_string(minimum - 1),
                 "MOV R" + std::to_string(registers) + ", 1",
                 "LOAD R0, " + std::to_string(memory),
                 std::string("STORE R0, -1")}) {
            scenario();
            reject([&] { (void)assemble(bad, config); }, "Architecture invalid boundary: " + bad);
        }
    }
}

// The oracle operates directly on independently generated operations. It does
// not use assembled operands, CPU helpers, or detail::number for its results.
enum class Op { Mov, Movr, Add, Sub, And, Or, Xor, Addi, Load, Store, Jmp, Jz, Jnz, Halt };
struct ReferenceInstruction {
    Op op;
    int destination = 0;
    int left = 0;
    int right = 0;
    std::int64_t value = 0;
};
std::string format(const ReferenceInstruction& instruction) {
    const auto r = [](int index) { return "R" + std::to_string(index); };
    const auto& i = instruction;
    switch (i.op) {
        case Op::Mov: return "MOV " + r(i.destination) + ", " + std::to_string(i.value);
        case Op::Movr: return "MOV " + r(i.destination) + ", " + r(i.left);
        case Op::Add: case Op::Sub: case Op::And: case Op::Or: case Op::Xor: {
            const char* name = i.op == Op::Add ? "ADD" : i.op == Op::Sub ? "SUB" : i.op == Op::And ? "AND" : i.op == Op::Or ? "OR" : "XOR";
            return std::string(name) + " " + r(i.destination) + ", " + r(i.left) + ", " + r(i.right);
        }
        case Op::Addi: return "ADDI " + r(i.destination) + ", " + r(i.left) + ", " + std::to_string(i.value);
        case Op::Load: return "LOAD " + r(i.destination) + ", " + std::to_string(i.value);
        case Op::Store: return "STORE " + r(i.left) + ", " + std::to_string(i.value);
        case Op::Jmp: return "JMP target_" + std::to_string(i.value);
        case Op::Jz: return "JZ " + r(i.left) + ", target_" + std::to_string(i.value);
        case Op::Jnz: return "JNZ " + r(i.left) + ", target_" + std::to_string(i.value);
        case Op::Halt: return "HALT";
    }
    throw std::runtime_error("Missing oracle operation");
}

void seeded_differential_programs() {
    constexpr std::uint32_t seed = 0xC11AB177u;
    std::mt19937 rng(seed);
    constexpr int instructions_per_program = 80;
    for (int bits : {8, 16, 32}) for (int registers : {4, 8, 16}) for (int memory : {64, 128, 256}) for (int sample = 0; sample < 10; ++sample) {
        scenario();
        const auto context = "Differential seed=0xC11AB177 architecture=" + std::to_string(bits) + "/" + std::to_string(registers) + "/" + std::to_string(memory) + " sample=" + std::to_string(sample);
        const Architecture config{"Differential", bits, registers, memory};
        const auto mask = word_mask(bits);
        std::vector<ReferenceInstruction> instructions;
        std::string source;
        for (int index = 0; index < instructions_per_program; ++index) {
            ReferenceInstruction i;
            i.op = index == instructions_per_program - 1 ? Op::Halt : index < registers ? Op::Mov : static_cast<Op>(rng() % 13);
            i.destination = index < registers ? index : static_cast<int>(rng() % registers);
            i.left = static_cast<int>(rng() % registers);
            i.right = static_cast<int>(rng() % registers);
            if (i.op == Op::Mov || i.op == Op::Addi) {
                const auto magnitude = static_cast<std::uint64_t>(rng()) & mask;
                i.value = (rng() % 3 == 0) ? -static_cast<std::int64_t>(magnitude & (mask >> 1)) : static_cast<std::int64_t>(magnitude);
            } else if (i.op == Op::Store || i.op == Op::Load) i.value = rng() % memory;
            else if (i.op == Op::Jmp || i.op == Op::Jz || i.op == Op::Jnz) {
                // Forward branches guarantee termination while exercising labels.
                const auto remaining = instructions_per_program - index - 1;
                i.value = index + 1 + rng() % static_cast<unsigned>(remaining);
            }
            instructions.push_back(i);
            source += "target_" + std::to_string(index) + ": " + format(i) + " ; generated\r\n";
        }
        auto cpu = make_cpu(source, config);
        std::vector<std::uint32_t> reference_registers(static_cast<std::size_t>(registers), 0);
        std::vector<std::uint32_t> reference_memory(static_cast<std::size_t>(memory), 0);
        std::size_t reference_pc = 0;
        std::uint64_t reference_cycles = 0;
        bool reference_halted = false;
        bool matched = true;
        while (!reference_halted) {
            const auto old_pc = reference_pc;
            const auto& i = instructions.at(reference_pc);
            auto next_pc = reference_pc + 1;
            std::map<std::size_t, std::uint32_t> register_writes, memory_writes;
            const auto write = [&](std::uint64_t value) { register_writes[static_cast<std::size_t>(i.destination)] = static_cast<std::uint32_t>(value & mask); };
            const auto a = reference_registers.at(static_cast<std::size_t>(i.left));
            const auto b = reference_registers.at(static_cast<std::size_t>(i.right));
            switch (i.op) {
                case Op::Mov: write(static_cast<std::uint64_t>(i.value)); break;
                case Op::Movr: write(a); break;
                case Op::Add: write(static_cast<std::uint64_t>(a) + b); break;
                case Op::Sub: write(static_cast<std::uint64_t>(a) + (mask + 1) - b); break;
                case Op::And: write(a & b); break;
                case Op::Or: write(a | b); break;
                case Op::Xor: write(a ^ b); break;
                case Op::Addi: write(static_cast<std::uint64_t>(static_cast<std::int64_t>(a) + i.value)); break;
                case Op::Load: write(reference_memory.at(static_cast<std::size_t>(i.value))); break;
                case Op::Store: memory_writes[static_cast<std::size_t>(i.value)] = a; break;
                case Op::Jmp: next_pc = static_cast<std::size_t>(i.value); break;
                case Op::Jz: if (a == 0) next_pc = static_cast<std::size_t>(i.value); break;
                case Op::Jnz: if (a != 0) next_pc = static_cast<std::size_t>(i.value); break;
                case Op::Halt: reference_halted = true; break;
            }
            for (const auto& change : register_writes) reference_registers[change.first] = change.second;
            for (const auto& change : memory_writes) reference_memory[change.first] = change.second;
            ++reference_cycles;
            reference_pc = next_pc;
            const auto entry = cpu.step();
            ++differential_steps;
            matched = matched && cpu.pc == reference_pc && cpu.cycles == reference_cycles && cpu.halted == reference_halted && cpu.registers == reference_registers && cpu.memory == reference_memory && cpu.trace.size() == reference_cycles && entry.pc == old_pc && entry.next_pc == next_pc && entry.cycle == reference_cycles && entry.halted == reference_halted && entry.register_changes == register_writes && entry.memory_changes == memory_writes;
            if (!matched) throw std::runtime_error(context + " instruction=" + std::to_string(old_pc) + " " + format(i));
        }
        require(matched, context);
    }
}

void seeded_arithmetic_aliases() {
    std::mt19937 rng(0xA11A5u);
    for (int bits : {8, 16, 32}) for (int sample = 0; sample < 256; ++sample) {
        scenario();
        const auto mask = word_mask(bits);
        const auto a = static_cast<std::uint64_t>(rng()) & mask;
        const auto b = static_cast<std::uint64_t>(rng()) & mask;
        const Architecture config{"Alias", bits, 4, 64};
        auto cpu = make_cpu("MOV R0, " + std::to_string(a) + "\nMOV R1, " + std::to_string(b) + "\nADD R0, R0, R1\nSUB R1, R1, R0\nAND R2, R0, R1\nOR R3, R0, R1\nXOR R0, R0, R0\nMOV R1, R1\nADDI R3, R3, -1\nSTORE R3, 0\nLOAD R3, 0\nHALT", config);
        cpu.run();
        const auto sum = (a + b) & mask;
        const auto difference = (b + mask + 1 - sum) & mask;
        const auto result = ((sum | difference) + mask) & mask;
        require(cpu.registers[0] == 0 && cpu.registers[1] == difference && cpu.registers[2] == (sum & difference) && cpu.registers[3] == result && cpu.memory[0] == result && cpu.cycles == 12, "Arithmetic with aliased source/destination in " + std::to_string(bits) + " bits sample=" + std::to_string(sample));
    }
}

void branches_and_step_resume() {
    for (const auto& opcode : {std::string("JZ"), std::string("JNZ")}) for (int condition : {0, 1, 255}) {
        scenario();
        auto cpu = make_cpu("MOV R0, " + std::to_string(condition) + "\n" + opcode + " R0, taken\nMOV R1, 17\nJMP end\ntaken: MOV R1, 23\nend: HALT");
        const bool taken = opcode == "JZ" ? condition == 0 : condition != 0;
        const auto trace = cpu.run();
        require(cpu.registers[1] == (taken ? 23u : 17u) && trace[1].next_pc == (taken ? 4u : 2u) && cpu.cycles == (taken ? 4u : 5u), "Conditional branch " + opcode + "=" + std::to_string(condition));
    }
    scenario();
    auto cpu = make_cpu("MOV R0, 255\nloop: ADDI R0, R0, -1\nJNZ R0, loop\nHALT");
    reject([&] { cpu.run(1); }, "First single-step batch reaches limit");
    reject([&] { cpu.run(500); }, "Loop batch reaches limit");
    require(cpu.cycles == 501 && cpu.trace.size() == 501 && !cpu.halted, "Batch retains progress");
    cpu.run(11);
    require(cpu.cycles == 512 && cpu.halted && cpu.registers[0] == 0, "Loop resumes exactly to HALT");
    scenario();
    cpu = make_cpu("JMP 0");
    reject([&] { cpu.run(1000); }, "Maximum batch limits infinite loop");
    require(cpu.cycles == 1000 && cpu.pc == 0 && cpu.trace.size() == 1000, "Exactly 1000 instructions run");
    reject([&] { cpu.run(0); }, "Zero batch rejected");
    reject([&] { cpu.run(1001); }, "Too large batch rejected");
    require(cpu.cycles == 1000, "Invalid batch preserves cycles");
}

void text_and_program_limits() {
    scenario();
    const std::string immediate_bom = "\xEF\xBB\xBF" "MOV R0, 7\r\nHALT\r\n";
    auto immediate_cpu = make_cpu(immediate_bom);
    immediate_cpu.run();
    require(immediate_cpu.program.source == immediate_bom && immediate_cpu.program.instructions[0].text() == "MOV R0, 7" && immediate_cpu.registers[0] == 7 && immediate_cpu.cycles == 2, "UTF-8 BOM immediately before opcode preserves source and readable trace");
    scenario();
    const std::string source = "\xEF\xBB\xBF" "# UTF-8 BOM\r\n\r\nBegin: mov r0, +0X0f ; comment\r\n  JMP End\r\nMOV R0, 0\r\nEnd:\r\nHALT\r\n";
    auto cpu = make_cpu(source);
    cpu.run();
    require(cpu.program.source == source && cpu.registers[0] == 15 && cpu.cycles == 3 && cpu.program.instructions[0].line == 3 && cpu.program.labels.at("End") == 3, "UTF-8 BOM, CRLF, original source and label lines");
    scenario();
    const auto exact_source = std::string("#") + std::string(MAX_SOURCE_LENGTH - 6, 'x') + "\nHALT";
    require(exact_source.size() == MAX_SOURCE_LENGTH && assemble(exact_source, {}).instructions.size() == 1, "Exact source byte limit accepted");
    scenario();
    reject([&] { (void)assemble(exact_source + " ", {}); }, "Source byte limit exceeded");
    scenario();
    // Keep the first line a comment after inserting BOM.
    auto bounded_bom = std::string("\xEF\xBB\xBF#") + std::string(MAX_SOURCE_LENGTH - 9, 'x') + "\nHALT";
    require(bounded_bom.size() == MAX_SOURCE_LENGTH && assemble(bounded_bom, {}).instructions.size() == 1, "BOM counts toward byte limit");
    scenario();
    reject([&] { (void)assemble(bounded_bom + " ", {}); }, "BOM does not bypass byte limit");
    scenario();
    std::string long_program;
    for (std::size_t i = 0; i < MAX_INSTRUCTIONS - 1; ++i) long_program += "ADDI R0, R0, 1\n";
    long_program += "HALT";
    cpu = make_cpu(long_program);
    reject([&] { cpu.run(1000); }, "1024-instruction program pauses at 1000");
    cpu.run(24);
    require(cpu.program.instructions.size() == MAX_INSTRUCTIONS && cpu.cycles == 1024 && cpu.registers[0] == 255 && cpu.halted, "Exact instruction limit runs and resumes");
    scenario();
    reject([&] { (void)assemble(long_program + "\nHALT", {}); }, "Instruction limit exceeded");
    const std::vector<std::string> invalid = {
        std::string("\xEF\xBB\xBF"), std::string("\xEF\xBB"), std::string("HALT\n\xEF\xBB\xBF"),
        std::string("\xC0\xAF"), std::string("\xED\xA0\x80"), std::string("\xF4\x90\x80\x80"), std::string("HALT\0", 5),
        "JMP Missing\nHALT", "JMP end\nend:", "same: HALT\nsame: HALT", "JMP Case\ncase: HALT",
        "MOV R0, 0x", "MOV R0, +", "MOV R0, -", "MOV R0, 0xGG", "MOV R0, 1,", "ADD R0,,R1", "HALT,", "MOV R000000000000000000000000000000000000000000000000099999999999999, 0"
    };
    for (const auto& bad : invalid) {
        scenario();
        reject([&] { (void)assemble(bad, {}); }, "Malformed text/opcode/operand/label rejected");
    }
    scenario();
    try { (void)assemble("# comment\r\n\r\nMOV R99, 1\r\nHALT", {}); throw std::runtime_error("Expected line-numbered diagnostic"); }
    catch (const DomainError& error) { require(std::string(error.what()).find("Linha 3:") == 0, "Error reports original CRLF source line"); }
}

void constructor_revalidates_manual_program() {
    scenario();
    Program forged;
    forged.source = "MOV R0, 7\nHALT";
    forged.instructions = {{"JMP", {999}, 1, "fake"}};
    forged.labels.emplace("fake", 999);
    CPU cpu({}, forged);
    cpu.run();
    require(cpu.registers[0] == 7 && cpu.cycles == 2 && cpu.program.labels.empty(), "Constructor reassembles authoritative source instead of forged instruction vectors");
    scenario();
    forged.source = "MOV R99, 1";
    reject([&] { CPU invalid({}, forged); }, "Forged program invalid source cannot bypass assembler");
    scenario();
    forged.source.clear();
    reject([&] { CPU invalid({}, forged); }, "Manual instructions do not bypass empty source validation");
}

// Defensive checks for publicly mutable C++ state. These are separate from
// source/program tests: normal GUI input cannot create these corruptions.
void corrupted_runtime_state_is_transactional() {
    const std::vector<std::pair<std::string, std::vector<std::int64_t>>> bad = {
        {"JMP", {999}}, {"JMP", {-1}}, {"JZ", {0, 999}}, {"JNZ", {0, -1}},
        {"MOV", {0, 256}}, {"MOV", {0, -129}}, {"MOV", {-1, 1}}, {"MOV", {8, 1}},
        {"MOVR", {0, 8}}, {"ADD", {0, 1}}, {"ADD", {0, 1, 2, 3}},
        {"ADDI", {0, 1, 256}}, {"LOAD", {0, 64}}, {"STORE", {0, -1}},
        {"HALT", {0}}, {"UNKNOWN", {}}
    };
    for (const auto& instruction : bad) {
        scenario();
        auto cpu = make_cpu("MOV R0, 1\nHALT");
        cpu.program.instructions[0].opcode = instruction.first;
        cpu.program.instructions[0].operands = instruction.second;
        const auto before_registers = cpu.registers;
        const auto before_memory = cpu.memory;
        reject([&] { cpu.step(); }, "Corrupt instruction " + instruction.first);
        require(cpu.pc == 0 && cpu.cycles == 0 && !cpu.halted && cpu.trace.empty() && cpu.registers == before_registers && cpu.memory == before_memory, "Invalid instruction preserves complete execution state");
    }
    for (const auto& corrupt : std::vector<std::function<void(CPU&)>>{
        [](CPU& cpu) { cpu.architecture.bits = 64; },
        [](CPU& cpu) { cpu.architecture.bits = -1; },
        [](CPU& cpu) { cpu.architecture.registers = 4; },
        [](CPU& cpu) { cpu.architecture.memory_size = 256; },
        [](CPU& cpu) { cpu.registers.clear(); },
        [](CPU& cpu) { cpu.memory.clear(); }
    }) {
        scenario();
        auto cpu = make_cpu("MOV R0, 1\nHALT");
        corrupt(cpu);
        reject([&] { cpu.step(); }, "Corrupt architecture or vector dimensions");
        require(cpu.pc == 0 && cpu.cycles == 0 && !cpu.halted && cpu.trace.empty(), "Invalid architecture preserves execution metadata");
    }
}
} // namespace

int main() {
    const std::array<std::pair<const char*, void(*)()>, 7> groups{{
        {"architecture_boundaries", architecture_boundaries},
        {"seeded_differential_programs", seeded_differential_programs},
        {"seeded_arithmetic_aliases", seeded_arithmetic_aliases},
        {"branches_and_step_resume", branches_and_step_resume},
        {"text_and_program_limits", text_and_program_limits},
        {"constructor_revalidates_manual_program", constructor_revalidates_manual_program},
        {"corrupted_runtime_state_is_transactional", corrupted_runtime_state_is_transactional}
    }};
    try {
        for (const auto& group : groups) {
            const auto start = scenarios;
            group.second();
            std::cout << "PASS " << group.first << " scenarios=" << scenarios - start << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL scenario=" << scenarios << ": " << error.what() << '\n';
        return 1;
    }
    std::cout << groups.size() << " groups; " << scenarios << " deterministic scenarios; " << assertions
              << " assertions; " << differential_steps << " independently compared CPU steps: OK\n";
    return 0;
}
