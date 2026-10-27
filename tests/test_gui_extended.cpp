#define wWinMain chiplab_application_entry
#include "../src/main.cpp"
#undef wWinMain
#include <iostream>
#include "win32_test_support.hpp"

int main() {
    unsigned checks=0;
    auto check=[&](bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);std::cout<<"PASS "<<label<<'\n';};
    auto rejects=[&](auto action,const char* label){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,label);};
    INITCOMMONCONTROLSEX init{sizeof(init),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&init);
    WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"ChipLabExtendedTests";RegisterClassW(&wc);
    App app;auto window=CreateWindowExW(0,wc.lpszClassName,L"ChipLab regression tests",WS_OVERLAPPEDWINDOW,0,0,1340,930,nullptr,nullptr,wc.hInstance,&app);
    if(!window){std::cerr<<"FAIL window creation\n";return 1;}
    try {
        auto evidence=testEvidenceDir(L"gui-extended");
        std::wstring emoji60;for(int i=0;i<60;++i)emoji60+=wide("\xF0\x9F\x94\xAC");
        SendMessageW(app.name,EM_SETSEL,0,-1);SendMessageW(app.name,EM_REPLACESEL,TRUE,(LPARAM)emoji60.c_str());
        check(textOf(app.name)==emoji60&&app.config().name==utf8(emoji60),"nome com 60 caracteres Unicode nao e cortado em 30");
        auto emoji61=emoji60+wide("\xF0\x9F\x94\xAC");
        SendMessageW(app.name,EM_SETSEL,0,-1);SendMessageW(app.name,EM_REPLACESEL,TRUE,(LPARAM)emoji61.c_str());
        rejects([&]{(void)app.config();},"61 caracteres rejeitados em vez de truncados para 60");
        setText(app.name,L"ChipLab");
        rejects([&]{(void)utf8(std::wstring(1,wchar_t(0xD800)));},"surrogate alto isolado rejeitado sem substituir texto");
        rejects([&]{(void)utf8(std::wstring(1,wchar_t(0xDC00)));},"surrogate baixo isolado rejeitado sem substituir texto");
        check(wide(utf8(emoji60))==emoji60,"Unicode valido preserva ida e volta");
        std::string source;for(int i=0;i<33332;++i)source+=";a\n";source+="HALT";
        auto win=windowsLines(source);SendMessageW(app.editor,EM_SETSEL,0,-1);SendMessageW(app.editor,EM_REPLACESEL,TRUE,(LPARAM)win.c_str());
        check(source.size()==100000&&app.sourceText()==source,"colagem real preserva 100000 bytes apos CRLF do controle");
        app.assembleNow(false);check(app.cpu->program.instructions.size()==1,"fonte colada no teto monta com HALT");
        setText(app.editor,windowsLines(source+";"));
        rejects([&]{app.assembleNow(false);},"100001 bytes rejeitados sem descartar o programa valido");
        check(app.cpu->program.source==source,"erro de tamanho preserva a montagem anterior");
        app.loadValues({},"JMP 0");setText(app.limit,L"1000");app.run();app.stop();
        check(!app.running&&textOf(app.state).find(L"EXECUTANDO")==std::wstring::npos,"parada atualiza o painel de execucao");
        check(IsWindowEnabled(app.buttons[0])&&!IsWindowEnabled(app.buttons[3]),"parada restaura botoes corretos");
        auto cycle=app.cpu->cycles;
        SendMessageW(app.limit,EM_SETSEL,0,-1);SendMessageW(app.limit,EM_REPLACESEL,TRUE,(LPARAM)L"10000");
        check(textOf(app.limit)==L"10000","entrada 10000 nao e truncada para 1000");
        rejects([&]{app.run();},"entrada 10000 e rejeitada pelo limite de execucao");
        check(!app.running&&app.cpu->cycles==cycle,"limite invalido preserva CPU e parada");
        std::runtime_error invalidError(std::string("falha \xFF"));
        app.error(invalidError,false);
        check(textOf(app.status).find(L"codificação inválida")!=std::wstring::npos,"tratamento de erro nao falha por mensagem fora de UTF8");
        auto beforeName=textOf(app.name),beforeSource=textOf(app.editor);
        rejects([&]{app.loadValues({"Nome",7,8,64},"HALT");},"arquitetura invalida rejeitada antes de alterar controles");
        check(textOf(app.name)==beforeName&&textOf(app.editor)==beforeSource&&app.cpu,"carregamento invalido preserva interface e CPU");
        rejects([&]{app.loadValues({},std::string("HALT\0",5));},"fonte NUL rejeitada antes de alterar controles");
        check(textOf(app.editor)==beforeSource,"fonte NUL nao e cortada silenciosamente no editor");
        auto file=evidence/L"saída-🔬.csv";
        app.writeFile(file.wstring(),"anterior\n");
        check(app.readFile(file.wstring())=="anterior\n","exportacao suporta caminho Unicode");
        HANDLE lock=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(lock!=INVALID_HANDLE_VALUE,"arquivo de exportacao bloqueado para ensaio");
        bool rejected=false;try{app.writeFile(file.wstring(),"novo");}catch(const std::exception&){rejected=true;}
        CloseHandle(lock);
        check(rejected&&app.readFile(file.wstring())=="anterior\n","falha na exportacao preserva o arquivo anterior");
        app.writeFile(file.wstring(),std::string(MAX_PROJECT_BYTES,'x'));
        check(app.readFile(file.wstring()).size()==MAX_PROJECT_BYTES,"leitura aceita exatamente 1 MiB");
        app.writeFile(file.wstring(),std::string(MAX_PROJECT_BYTES+1,'x'));
        rejects([&]{(void)app.readFile(file.wstring());},"leitura rejeita 1 MiB mais 1 sem alocacao ilimitada");
        rejects([&]{(void)app.readFile((evidence/L"ausente.asm").wstring());},"leitura de arquivo ausente devolve erro controlado");
        app.loadValues({},"\xEF\xBB\xBFMOV R0, 15\nSTORE R0, 0\nHALT");setText(app.limit,L"1000");app.run();while(app.running)app.tick();
        check(app.cpu->halted&&app.cpu->memory[0]==15,"programa BOM executa pela interface");
        auto csv=app.traceCsv();app.writeFile((evidence/L"bom_rastro.csv").wstring(),csv);
        check(csv.find("1,0,\"MOV R0, 15\"")!=std::string::npos,"BOM nao contamina instrucao no rastro CSV");
        app.loadValues({},"MOV R0, 5\nHALT");
        rejects([&]{(void)app.traceCsv();},"exportacao sem passos informa ausencia de rastro");
        app.step();app.run();while(app.running)app.tick();
        auto oldCycles=app.cpu->cycles,oldTrace=app.cpu->trace.size();app.step();
        check(app.cpu->halted&&app.cpu->cycles==oldCycles&&app.cpu->trace.size()==oldTrace,"Passo apos HALT nao duplica rastro");
        std::cout<<checks<<" verificacoes estendidas Win32: OK\n";
        app.modified=false;DestroyWindow(window);return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<" apos "<<checks<<" verificacoes\n";app.modified=false;DestroyWindow(window);return 1;}
}
