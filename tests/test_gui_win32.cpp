// Integração de controles Win32 reais, em janela oculta, sem ferramentas externas.
#define wWinMain chiplab_application_entry
#include "../src/main.cpp"
#undef wWinMain
#include <chrono>
#include <iostream>
#include "win32_test_support.hpp"

int main() {
    unsigned checks=0;
    auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);++checks;std::cout<<"PASS "<<name<<'\n';};
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX init{sizeof(init),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&init);
    WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"ChipLabIntegrationTests";RegisterClassW(&wc);
    App app;
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Testes de integração ChipLab",WS_OVERLAPPEDWINDOW,0,0,1340,930,nullptr,nullptr,wc.hInstance,&app);
    if(!window){std::cerr<<"Nao foi possivel criar controles Win32\n";return 1;}
    try {
        check(app.cpu&&app.cpu->cycles==0,"inicializacao da janela e CPU");
        app.step();check(app.cpu->cycles==1&&app.cpu->pc==1&&app.cpu->registers[0]==5,"Passo atualiza CPU");
        app.assembleNow(false);app.run();while(app.running)app.tick();
        check(app.cpu->halted&&app.cpu->cycles==20&&app.cpu->pc==8&&app.cpu->registers[1]==15&&app.cpu->registers[2]==15&&app.cpu->memory[0]==15,"soma completa pela interface");
        auto csv=app.traceCsv();
        check(std::count(csv.begin(),csv.end(),'\n')==21&&csv.find("20,7,\"HALT\",8,\"HALT\",1")!=std::string::npos,"CSV real com 20 registros e HALT");
        check(App::csvQuote("a,\"b\"")=="\"a,\"\"b\"\"\"","CSV escapa virgulas e aspas");
        fs::path evidence=testEvidenceDir(L"gui");
        app.writeFile((evidence/"soma_rastro.csv").wstring(),csv);
        save_project(evidence/"soma.chiplab.json",app.config(),app.sourceText());
        app.writeFile((evidence/"chiplab_alu.v").wstring(),export_verilog(app.config()));
        setText(app.editor,L"JMP 0");setText(app.limit,L"10");app.run();while(app.running)app.tick();
        check(app.cpu->cycles==10&&app.cpu->pc==0&&!app.cpu->halted,"limite 10 com remontagem automatica");
        app.run();while(app.running)app.tick();check(app.cpu->cycles==20,"retomada preserva estado apos limite");
        setText(app.limit,L"0");bool rejected=false;try{app.run();}catch(const std::exception&){rejected=true;}
        check(rejected&&app.cpu->cycles==20,"limite zero rejeitado sem alterar CPU");
        setText(app.limit,L"1001");rejected=false;try{app.run();}catch(const std::exception&){rejected=true;}
        check(rejected&&app.cpu->cycles==20,"limite 1001 rejeitado sem alterar CPU");
        setText(app.editor,L"MOV R99, 1\r\nHALT");rejected=false;try{app.assembleNow(false);}catch(const std::exception&){rejected=true;}
        check(rejected&&app.cpu->cycles==20&&app.cpu->program.source=="JMP 0","montagem invalida preserva programa e CPU");
        std::string limitSource;for(int i=0;i<33332;++i)limitSource+=";a\n";limitSource+="HALT";
        check(limitSource.size()==100000,"fonte valida no teto de 100000 bytes");
        setText(app.editor,windowsLines(limitSource));
        check(app.sourceText()==limitSource,"editor normaliza CRLF para LF sem crescer fonte");
        app.assembleNow(false);check(app.cpu->program.instructions.size()==1,"fonte no teto monta apos normalizacao Win32");
        save_project(evidence/"limite_fonte.chiplab.json",app.config(),app.sourceText());
        check(load_project(evidence/"limite_fonte.chiplab.json").second==limitSource,"fonte no teto salva e reabre");
        Architecture wideConfig{"CPU 32 bits",32,16,256};app.loadValues(wideConfig,"MOV R15, 4294967295\nSTORE R15, 255\nHALT");setText(app.limit,L"1000");
        app.run();while(app.running)app.tick();check(app.cpu->registers[15]==4294967295u&&app.cpu->memory[255]==4294967295u,"configuracao 32 bits e ultimo endereco");
        app.loadValues({},"JMP 0");setText(app.limit,L"1000");
        auto start=std::chrono::steady_clock::now();app.run();
        while(app.running){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(1);}
        auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        check(app.cpu->cycles==1000,"lote de 1000 com temporizador real Win32");
        std::cout<<"TEMPO_GUI_1000_MS "<<elapsed<<"\n";
        // Benchmark separado da correção funcional: o tempo depende do computador
        // e da carga do sistema. Conservar a medição sem tornar a suíte intermitente.
        std::cout<<"BENCHMARK_META_2000_MS "<<(elapsed<=2000?"ATENDIDA":"EXCEDIDA")<<'\n';
        app.assembleNow(false);check(app.cpu->cycles==0&&app.cpu->trace.empty()&&app.cpu->memory[0]==0,"Reiniciar zera estado e rastro");
        app.modified=false;DestroyWindow(window);
        std::cout<<checks<<" verificacoes de integracao Win32: OK\n";
        return 0;
    }catch(const std::exception& error){app.modified=false;DestroyWindow(window);std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
