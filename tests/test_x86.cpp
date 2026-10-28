#include "../src/x86.hpp"
#include <functional>
#include <iostream>

namespace x86 = chiplab::x86;

namespace {
unsigned checks = 0;
unsigned groups = 0;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error("FAIL " + message);
}
void rejects(const std::function<void()>& action, const std::string& message) {
    bool failed = false;
    try { action(); } catch (const chiplab::DomainError&) { failed = true; }
    check(failed, message);
}
x86::CPU machine(const std::string& source) {
    const auto runtime = x86::runtime_directory();
    return x86::CPU(x86::assemble(source, runtime), runtime);
}
std::uint32_t reg(const x86::CPU& cpu, std::size_t index) { return cpu.snapshot().regs.at(index); }
bool flag(const x86::CPU& cpu, unsigned bit) { return (cpu.snapshot().eflags & (std::uint32_t{1} << bit)) != 0; }
void run_group(const char* name, const std::function<void()>& action) {
    action(); ++groups; std::cout << "PASS " << name << '\n';
}

void arithmetic_and_flags() {
    auto cpu = machine("mov eax,0x7fffffff\nadd eax,1\nhlt");
    cpu.step(); cpu.step();
    check(reg(cpu,0)==0x80000000u, "32-bit overflow wraps in EAX");
    check(flag(cpu,11)&&flag(cpu,7)&&!flag(cpu,0)&&!flag(cpu,6), "OF SF CF ZF reflect signed overflow");
    check(cpu.cycles==2&&!cpu.halted, "one native instruction per step");
    auto carry = machine("mov eax,0xffffffff\nadd eax,1\nhlt");
    carry.step(); carry.step();
    check(reg(carry,0)==0&&flag(carry,0)&&flag(carry,6)&&!flag(carry,11), "unsigned carry differs from signed overflow");
    auto borrow = machine("xor eax,eax\nsub eax,1\nhlt");
    borrow.step(); borrow.step();
    check(reg(borrow,0)==0xffffffffu&&flag(borrow,0)&&flag(borrow,7), "SUB borrow and sign flags");
    auto adc = machine("mov eax,0xffffffff\nadd eax,1\nmov ebx,10\nadc ebx,0\nhlt");
    adc.run(100);
    check(reg(adc,1)==11&&adc.halted, "ADC consumes the real carry flag");
    auto byte=machine("mov eax,0x123400ff\nadd al,1\nmov al,127\nadd al,1\nhlt");
    byte.step();byte.step();
    check(reg(byte,0)==0x12340000u&&flag(byte,0)&&flag(byte,6)&&!flag(byte,11), "8-bit ADD wraps only AL and computes 8-bit flags");
    byte.step();byte.step();
    check(reg(byte,0)==0x12340080u&&!flag(byte,0)&&flag(byte,7)&&flag(byte,11), "8-bit signed overflow uses bit 7 rather than bit 31");
    auto word=machine("mov eax,0x1234ffff\nadd ax,1\nhlt");word.step();word.step();
    check(reg(word,0)==0x12340000u&&flag(word,0)&&flag(word,6), "16-bit ADD preserves upper EAX half");
}

void subregisters_and_native_operations() {
    auto cpu = machine("mov eax,0x12345678\nmov al,0xaa\nmov ah,0xbb\nmov ax,0xccdd\nhlt");
    cpu.step(); cpu.step(); check(reg(cpu,0)==0x123456aau, "AL aliases low byte");
    cpu.step(); check(reg(cpu,0)==0x1234bbaau, "AH aliases second byte");
    cpu.step(); check(reg(cpu,0)==0x1234ccddu, "AX preserves high 16 bits");
    auto native = machine("mov eax,7\nimul eax,eax,6\nmov ebx,3\nxchg eax,ebx\nshl eax,2\nmovzx ecx,al\nmov edx,-1\nmovsx esi,dl\nhlt");
    native.run(100);
    check(reg(native,0)==12&&reg(native,1)==42&&reg(native,2)==12&&reg(native,4)==0xffffffffu, "IMUL XCHG SHL MOVZX MOVSX execute natively");
    auto division=machine("mov eax,43\nxor edx,edx\nmov ebx,6\ndiv ebx\nhlt");
    division.run(100);
    check(reg(division,0)==7&&reg(division,3)==1, "DIV returns quotient EAX and remainder EDX");
}

void branches_signed_unsigned_and_loop() {
    auto cpu=machine("mov eax,-2\nmov ebx,0\ncmp eax,1\njl signed_less\nmov ebx,99\nsigned_less: or ebx,1\ncmp eax,1\nja unsigned_above\nmov ebx,99\nunsigned_above: or ebx,2\nmov ecx,5\nxor edx,edx\nagain: add edx,ecx\nloop again\nhlt");
    cpu.run(100);
    check(reg(cpu,1)==3, "JL signed and JA unsigned use different comparisons");
    check(reg(cpu,2)==0&&reg(cpu,3)==15, "LOOP decrements ECX and jumps");
    check(cpu.halted, "forward and backward NASM labels resolve");
}

void stack_calls_and_memory() {
    auto cpu=machine("mov eax,7\ncall twice\nmov [0x110000],eax\npush eax\nxor eax,eax\npop ebx\nhlt\ntwice: push ebp\nmov ebp,esp\nadd eax,eax\nmov esp,ebp\npop ebp\nret");
    const auto esp=reg(cpu,7);
    cpu.run(100);
    check(reg(cpu,0)==0&&reg(cpu,1)==14&&reg(cpu,7)==esp, "CALL RET PUSH POP restore the real ESP");
    check(cpu.readMemory(0x110000,4)==std::vector<std::uint8_t>({14,0,0,0}), "little-endian DWORD store");
    auto alias=machine("mov dword [0x110000],0x12345678\nmov byte [0x110001],0xaa\nmov eax,[0x110000]\nmovzx ebx,word [0x110002]\nlea esi,[eax+ebx*4+8]\nhlt");
    alias.run(100);
    check(reg(alias,0)==0x1234aa78&&reg(alias,1)==0x1234, "byte word and dword address the same memory bytes");
    check(reg(alias,4)==0x1234f350u, "LEA evaluates scaled index and displacement");
    const auto top=x86::MEMORY_BASE+x86::MEMORY_SIZE;
    check(alias.readMemory(top-1,1).size()==1, "last mapped byte is readable");
    rejects([&]{(void)alias.readMemory(top,1);}, "read beyond memory rejected");
    rejects([&]{(void)alias.readMemory(0,4);}, "unmapped read rejected");
}

void nasm_sections_macros_and_syscalls() {
    auto cpu=machine("%define STDOUT 1\n%macro load 2\n mov %1,%2\n%endmacro\nsection .data\nmessage: db 'Ola, ChipLab!',10\nlength equ $-message\nsection .bss\ncopy: resb 16\nsection .text\nglobal _start\n_start:\nload eax,4\nload ebx,STDOUT\nmov ecx,message\nmov edx,length\nint 0x80\nmov esi,message\nmov edi,copy\nmov ecx,length\ncld\nrep movsb\nmov eax,1\nxor ebx,ebx\nint 0x80");
    cpu.run(100);
    check(cpu.output=="Ola, ChipLab!\n", "Linux int80 write captures exact output");
    check(cpu.halted&&!cpu.halt_reason.empty(), "Linux int80 exit stops cleanly");
    check(reg(cpu,2)==0, "REP MOVSB completes ECX iterations");
    check(cpu.readMemory(reg(cpu,5)-14,14)==std::vector<std::uint8_t>({'O','l','a',',',' ','C','h','i','p','L','a','b','!','\n'}), "NASM data bss and REP copy preserve bytes");
}

void trace_and_bounded_execution() {
    auto cpu=machine("mov eax,5\nadd eax,3\nmov [0x110000],eax\nhlt");
    const auto initial=cpu.pc();
    const auto first=cpu.step();
    check(first.pc==initial&&first.next_pc==initial+5&&cpu.pc()==initial+5, "EIP advances by encoded byte length");
    check(!first.bytes.empty()&&!first.instruction.empty(), "trace contains machine bytes and disassembly");
    check(first.register_changes.at("EAX")==5, "trace names real changed register");
    cpu.step(); const auto store=cpu.step();
    check(store.memory_changes.at(0x110000)==8, "trace records byte-addressed memory write");
    cpu.step();
    check(cpu.halted&&cpu.cycles==4&&cpu.trace.size()==4&&cpu.trace.back().halted, "HLT recorded once");
    const auto cycles=cpu.cycles;
    rejects([&]{cpu.step();}, "step after HLT rejected");
    check(cpu.cycles==cycles&&cpu.trace.size()==4, "step after HLT preserves trace");
    auto loop=machine("again: inc eax\njmp again");
    loop.run(7);
    check(loop.cycles==7&&reg(loop,0)==4&&loop.trace.size()==7, "execution limit preserves correct partial progress");
    rejects([&]{loop.run(0);}, "zero execution budget rejected");
    rejects([&]{loop.run(1001);}, "excessive execution budget rejected");
    auto resume=machine("mov ecx,4\nagain: dec ecx\njnz again\nhlt");
    resume.run(3);
    check(!resume.halted&&resume.cycles==3, "finite program pauses normally at budget");
    resume.run(20);
    check(resume.halted&&reg(resume,2)==0, "execution resumes after limit");
}

void controlled_errors_and_project() {
    const auto runtime=x86::runtime_directory();
    for(const auto& source: {std::string{}, std::string("banana eax,1"),std::string("mov eax,"),std::string("mov rax,1\nhlt"),std::string("%include 'C:/Windows/win.ini'\nhlt")}) {
        rejects([&]{(void)x86::assemble(source,runtime);}, "invalid or unsupported NASM input rejected");
    }
    bool firstLine=false;
    try{(void)x86::assemble("mov eax,\nhlt",runtime);}catch(const chiplab::DomainError& error){firstLine=std::string(error.what()).find(":1:")!=std::string::npos;}
    check(firstLine, "NASM diagnostic reports user line 1 rather than wrapper line");
    rejects([&]{(void)x86::assemble(std::string(chiplab::MAX_SOURCE_LENGTH+1,'a'),runtime);}, "source byte limit enforced");
    auto invalid_memory=machine("mov eax,[0]\nhlt");
    rejects([&]{invalid_memory.step();}, "unmapped native memory access becomes DomainError");
    auto divide=machine("mov eax,5\nxor edx,edx\nxor ebx,ebx\ndiv ebx\nhlt");
    divide.step(); divide.step(); divide.step();
    const auto before=divide.snapshot();const auto beforeCycles=divide.cycles;
    rejects([&]{divide.step();}, "division exception becomes DomainError");
    check(divide.snapshot().regs==before.regs&&divide.snapshot().eip==before.eip&&divide.snapshot().eflags==before.eflags&&divide.cycles==beforeCycles, "faulting DIV rolls back complete architectural state");
    auto boundary=machine("mov edi,0x200fff\nmov eax,0x11223344\nmov [edi],eax\nhlt");
    boundary.step();boundary.step();const auto memoryBefore=boundary.readMemory(0x200ffc,4);const auto boundaryBefore=boundary.snapshot();
    rejects([&]{boundary.step();}, "native write across map boundary rejected");
    check(boundary.readMemory(0x200ffc,4)==memoryBefore&&boundary.pc()==boundaryBefore.eip&&boundary.cycles==2, "partial cross-boundary write rolls back mapped bytes");
    auto repBoundary=machine("mov edi,0x200ffc\nmov eax,0x44\nmov ecx,16\ncld\nrep stosb\nhlt");
    for(int i=0;i<4;++i)repBoundary.step();
    const auto repBefore=repBoundary.snapshot();const auto repMemory=repBoundary.readMemory(0x200ffc,4);
    rejects([&]{repBoundary.step();}, "REP write across map boundary rejected");
    check(repBoundary.readMemory(0x200ffc,4)==repMemory&&repBoundary.snapshot().regs==repBefore.regs&&repBoundary.pc()==repBefore.eip&&repBoundary.cycles==4, "faulting REP restores memory ECX EDI EIP and history");
    const std::string source="section .text\n_start:\n mov eax,42\n hlt\n";
    const auto json=x86::project_json("Projeto x86",source);
    check(x86::is_project(json), "version2 x86 project recognized");
    const auto loaded=x86::parse_project(json);
    check(loaded.first=="Projeto x86"&&loaded.second==source, "x86 JSON preserves mode name and exact source");
    check(!x86::is_project("{\"version\":1,\"architecture\":{},\"source\":\"HALT\"}"), "legacy JSON is not misidentified as x86");
    rejects([&]{(void)x86::parse_project("{\"version\":2,\"mode\":\"arm\",\"name\":\"X\",\"source\":\"hlt\"}");}, "unsupported project ISA rejected");
    rejects([&]{(void)x86::parse_project("{\"version\":2,\"mode\":\"x86-32\",\"name\":\"X\",\"source\":null}");}, "project source type validated");
}
}

int main() {
    try {
        run_group("arithmetic_and_flags",arithmetic_and_flags);
        run_group("subregisters_and_native_operations",subregisters_and_native_operations);
        run_group("branches_signed_unsigned_and_loop",branches_signed_unsigned_and_loop);
        run_group("stack_calls_and_memory",stack_calls_and_memory);
        run_group("nasm_sections_macros_and_syscalls",nasm_sections_macros_and_syscalls);
        run_group("trace_and_bounded_execution",trace_and_bounded_execution);
        run_group("controlled_errors_and_project",controlled_errors_and_project);
        std::cout<<groups<<" x86 groups; "<<checks<<" assertions: OK\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<" after "<<checks<<" assertions\n"; return 1; }
}
