#pragma once
#include <chrono>

// Os testes guardam suas saídas em uma pasta explícita do build ou em uma
// pasta temporária exclusiva. Nunca usam evidencias/ do projeto por padrão.
inline std::filesystem::path testEvidenceDir(const wchar_t* suite) {
    std::filesystem::path result;
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args)throw std::runtime_error("Nao foi possivel ler os argumentos do teste.");
    for(int i=1;i<count;++i) {
        if(std::wstring(args[i])==L"--evidence-dir"&&i+1<count)result=args[++i];
        else {LocalFree(args);throw std::runtime_error("Use --evidence-dir seguido de uma pasta de teste.");}
    }
    LocalFree(args);
    if(result.empty())result=std::filesystem::temp_directory_path()/
        (std::wstring(L"ChipLab-")+suite+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+
         std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(result);
    std::cout<<"EVIDENCE_DIR "<<utf8(result.wstring())<<'\n';
    return result;
}
