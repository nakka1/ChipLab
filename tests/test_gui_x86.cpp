#define wWinMain chiplab_application_entry
#include "../src/main.cpp"
#undef wWinMain
#include <iostream>
#include "win32_test_support.hpp"

namespace {
std::wstring cell(HWND table,int row,int column) {
    wchar_t value[4096]{};ListView_GetItemText(table,row,column,value,4096);return value;
}
}

int main() {
    unsigned checks=0;
    auto check=[&](bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);std::cout<<"PASS "<<message<<'\n';};
    auto rejects=[&](auto action,const char* message){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,message);};
    INITCOMMONCONTROLSEX init{sizeof(init),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&init);
    WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"ChipLabX86Tests";RegisterClassW(&wc);
    App app;auto window=CreateWindowExW(0,wc.lpszClassName,L"ChipLab x86 integration tests",WS_OVERLAPPEDWINDOW,0,0,1340,930,nullptr,nullptr,wc.hInstance,&app);
    if(!window){std::cerr<<"FAIL window creation\n";return 1;}
    try {
        auto evidence=testEvidenceDir(L"gui-x86");
        check(!app.x86Mode()&&app.cpu, "legacy mode still initializes without x86 runtime loading");
        app.setX86Mode(true,true);app.assembleNow(false);app.run();while(app.running)app.tick();
        check(app.x86cpu&&app.x86cpu->halted&&app.x86cpu->snapshot().regs[0]==15, "default x86 example selected by mode switch assembles and sums to 15");
        app.loadValues({"Minha CPU",16,4,128},"MOV R0,9\nHALT");app.assembleNow(false);
        auto oldSource=app.sourceText();
        app.setX86Mode(true,false);
        check(app.x86Mode()&&app.sourceText()==oldSource, "switching ISA preserves custom editor text");
        check(!IsWindowEnabled(app.bits)&&!IsWindowEnabled(app.regs)&&!IsWindowEnabled(app.mem), "x86 fixes architecture controls");
        check((GetMenuState(GetMenu(window),ID_VERILOG,MF_BYCOMMAND)&MF_GRAYED)!=0, "didactic ULA export disabled for x86");
        app.setX86Mode(false,false);
        check(app.config().bits==16&&app.config().registers==4&&app.config().memory_size==128, "returning to didactic restores architecture selection");
        const std::string source="mov eax,0x7fffffff\nadd eax,1\nmov [0x110000],eax\nhlt";
        app.loadX86Values("Experimento x86",source);app.assembleNow(false);
        check(app.x86Mode()&&app.x86cpu&&!app.cpu&&app.x86cpu->cycles==0, "x86 project initializes separate native CPU");
        check(cell(app.regTable,0,0)==L"EAX"&&cell(app.regTable,7,0)==L"ESP", "table shows real x86 register names");
        app.step();app.step();
        check(app.x86cpu->snapshot().regs[0]==0x80000000u&&app.x86cpu->cycles==2, "Passo executes real x86 arithmetic");
        check(textOf(app.state).find(L"OF=1")!=std::wstring::npos&&textOf(app.state).find(L"EIP")!=std::wstring::npos, "panel displays real EIP and overflow flag");
        check(ListView_GetItemCount(app.traceTable)==2&&cell(app.traceTable,0,2).find(L" | ")!=std::wstring::npos, "trace renders machine bytes alongside Intel assembly");
        setText(app.limit,L"1000");app.run();while(app.running)app.tick();
        check(app.x86cpu->halted&&app.x86cpu->cycles==4, "Executar reaches HLT with four native instructions");
        setText(app.memoryAddress,L"0x110000");app.viewMemory();
        check(cell(app.memTable,0,0)==L"0x00110000"&&cell(app.memTable,3,1)==L"128", "memory table renders byte addresses and little-endian value");
        setText(app.memoryAddress,L"0x200fff");app.viewMemory();
        check(ListView_GetItemCount(app.memTable)==1, "memory view clamps length at last mapped byte");
        setText(app.memoryAddress,L"0x201000");
        rejects([&]{app.viewMemory();}, "memory view rejects address after mapped region");
        check(app.memoryStart==0x200fff, "invalid memory address preserves prior view");
        const auto csv=app.traceCsv();app.writeFile((evidence/L"x86_rastro.csv").wstring(),csv);
        check(csv.find("EAX")!=std::string::npos&&csv.find("hlt")!=std::string::npos, "CSV exports x86 trace rather than legacy CPU");
        const auto traceSize=app.x86cpu->trace.size();app.step();
        check(app.x86cpu->trace.size()==traceSize, "Passo after HLT does not duplicate native trace");
        auto* previous=app.x86cpu.get();setText(app.editor,L"mov eax,");
        rejects([&]{app.assembleNow(false);}, "invalid NASM input raises controlled assembly error");
        check(app.x86cpu.get()==previous&&app.x86cpu->halted, "invalid assembly preserves prior native CPU state");
        app.loadX86Values("Saida","section .data\nmsg: db 'Teste x86',10\nlen equ $-msg\nsection .text\nglobal _start\n_start: mov eax,4\nmov ebx,1\nmov ecx,msg\nmov edx,len\nint 0x80\nmov eax,1\nxor ebx,ebx\nint 0x80");
        app.assembleNow(false);app.run();while(app.running)app.tick();
        check(app.x86cpu->output=="Teste x86\n"&&textOf(app.console).find(L"Teste x86")!=std::wstring::npos, "int80 output reaches visible console control");
        app.loadX86Values("Laco","again: inc eax\njmp again");app.assembleNow(false);setText(app.limit,L"7");app.run();while(app.running)app.tick();
        check(app.x86cpu->cycles==7&&!app.x86cpu->halted&&!app.running, "GUI budget stops looping native program normally");
        app.run();while(app.running)app.tick();
        check(app.x86cpu->cycles==14&&app.x86cpu->snapshot().regs[0]==7, "GUI resumes native EIP without remounting");
        app.assembleNow(false);
        check(app.x86cpu->cycles==0&&app.x86cpu->trace.empty()&&app.x86cpu->output.empty(), "Reiniciar remount clears CPU trace and console");
        app.setX86Mode(false,false);app.loadValues({},"MOV R0,15\nSTORE R0,0\nHALT");app.assembleNow(false);setText(app.limit,L"1000");app.run();while(app.running)app.tick();
        check(!app.x86Mode()&&app.cpu->halted&&app.cpu->memory[0]==15, "didactic execution works after multiple native projects");
        check(IsWindowEnabled(app.bits)&&(GetMenuState(GetMenu(window),ID_VERILOG,MF_BYCOMMAND)&MF_GRAYED)==0, "legacy architecture and ULA export restored");
        std::cout<<checks<<" x86 Win32 integration assertions: OK\n";
        app.modified=false;DestroyWindow(window);return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<" after "<<checks<<" assertions\n";app.modified=false;DestroyWindow(window);return 1;}
}
