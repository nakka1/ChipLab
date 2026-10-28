#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include "core.hpp"
#include "x86.hpp"

using namespace chiplab;
namespace fs = std::filesystem;

static const wchar_t* DEFAULT_SOURCE =
L"; Soma de 1 a 5: resultado 15 na memória[0].\r\n"
L"MOV R0, 5\r\nMOV R1, 0\r\n"
L"loop: ADD R1, R1, R0\r\nADDI R0, R0, -1\r\nJNZ R0, loop\r\n"
L"STORE R1, 0\r\nLOAD R2, 0\r\nHALT\r\n";

static const wchar_t* X86_SOURCE =
L"; NASM / Intel, x86 de 32 bits. Soma de 1 a 5.\r\n"
L"BITS 32\r\nsection .text\r\n"
L"start:\r\n    xor eax, eax\r\n    mov ecx, 5\r\n"
L".soma:\r\n    add eax, ecx\r\n    dec ecx\r\n    jnz .soma\r\n"
L"    mov [resultado], eax\r\n    hlt\r\n"
L"section .data\r\nresultado: dd 0\r\n";

enum { ID_NAME=101, ID_BITS, ID_REGS, ID_MEMORY, ID_EDITOR, ID_LIMIT,
       ID_ASSEMBLE=201, ID_STEP, ID_RUN, ID_PAUSE, ID_RESET,
       ID_NEW=301, ID_OPEN, ID_SAVE, ID_ASM, ID_CSV, ID_VERILOG, ID_HELP, ID_EXIT };
enum { ID_MODE=108, ID_MEM_START=109, ID_MEM_VIEW=110 };

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n) throw std::runtime_error("Texto não está em UTF-8 válido.");
    std::wstring w(n,0); MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),w.data(),n); return w;
}
std::string utf8(const std::wstring& w) {
    if(w.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);
    if(!n)throw std::runtime_error("Texto contém uma sequência Unicode inválida.");
    std::string s(n,0); WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr); return s;
}
std::wstring diagnosticText(const std::exception& error) {
    try {return wide(error.what());}
    catch(const std::exception&) {return L"Não foi possível concluir a operação. A mensagem de erro contém codificação inválida.";}
}
std::wstring windowsLines(const std::string& source) {
    auto original=wide(source); std::wstring result;
    for(size_t i=0;i<original.size();++i) {
        wchar_t c=original[i];
        if(c==L'\r') { if(i+1<original.size()&&original[i+1]==L'\n')++i;result+=L"\r\n"; }
        else if(c==L'\n')result+=L"\r\n";
        else result+=c;
    }
    return result;
}
std::wstring textOf(HWND h) {
    int n=GetWindowTextLengthW(h); std::wstring w(n+1,0); GetWindowTextW(h,w.data(),n+1); w.resize(n); return w;
}
void setText(HWND h, const std::wstring& s) { SetWindowTextW(h,s.c_str()); }

class App {
public:
    HWND window{}, name{}, bits{}, regs{}, mem{}, limit{}, editor{}, state{}, regTable{}, memTable{}, traceTable{};
    HWND mode{}, modeLabel{}, console{}, consoleLabel{}, memoryAddress{}, memoryView{};
    HWND title{}, subtitle{}, hint{}, configLabels[5]{}, editorLabel{}, regsLabel{}, memLabel{}, traceLabel{}, status{};
    HWND buttons[5]{};
    HFONT font{}, mono{}, heading{}, smallFont{};
    HBRUSH background=CreateSolidBrush(RGB(244,246,250)), white=CreateSolidBrush(RGB(255,255,255));
    Architecture architecture;
    std::unique_ptr<CPU> cpu;
    std::unique_ptr<x86::CPU> x86cpu;
    Architecture didacticSelection;
    bool useX86=false;
    uint32_t memoryStart=x86::MEMORY_BASE;
    std::string assembledName;
    bool dirty=true, running=false, modified=false, updating=false;
    size_t remaining=0, renderedTrace=0;
    std::wstring projectPath;

    ~App() { DeleteObject(background);DeleteObject(white);DeleteObject(font);DeleteObject(mono);DeleteObject(heading);DeleteObject(smallFont); }
    HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int id=0) {
        HWND h=CreateWindowExW((wcscmp(cls,L"EDIT")==0 || wcscmp(cls,WC_LISTVIEWW)==0)?WS_EX_CLIENTEDGE:0,
            cls,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);return h;
    }
    HWND table(std::initializer_list<std::pair<const wchar_t*,int>> cols) {
        HWND h=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP);
        ListView_SetExtendedListViewStyle(h,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);
        SendMessageW(h,WM_SETFONT,(WPARAM)mono,TRUE);
        int i=0; for(auto& c:cols) { LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=(LPWSTR)c.first;col.cx=c.second;ListView_InsertColumn(h,i++,&col); }
        return h;
    }
    void row(HWND h,int index,std::initializer_list<std::wstring> cells) {
        int col=0;for(auto& t:cells) { if(!col){LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=index;item.pszText=(LPWSTR)t.c_str();ListView_InsertItem(h,&item);}else ListView_SetItemText(h,index,col,(LPWSTR)t.c_str()); ++col; }
    }
    void initialize(HWND h) {
        window=h;
        font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        mono=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Consolas");
        heading=CreateFontW(-30,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        smallFont=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        HMENU menu=CreateMenu(), file=CreatePopupMenu(), exp=CreatePopupMenu();
        AppendMenuW(file,MF_STRING,ID_NEW,L"Novo exemplo");AppendMenuW(file,MF_STRING,ID_OPEN,L"Abrir projeto JSON...");
        AppendMenuW(file,MF_STRING,ID_SAVE,L"Salvar projeto JSON...");AppendMenuW(file,MF_STRING,ID_ASM,L"Abrir programa .asm...");
        AppendMenuW(file,MF_SEPARATOR,0,nullptr);AppendMenuW(file,MF_STRING,ID_EXIT,L"Sair");
        AppendMenuW(exp,MF_STRING,ID_CSV,L"Rastro de execução (CSV)...");AppendMenuW(exp,MF_STRING,ID_VERILOG,L"ULA combinacional (Verilog)...");
        AppendMenuW(menu,MF_POPUP,(UINT_PTR)file,L"Arquivo");AppendMenuW(menu,MF_POPUP,(UINT_PTR)exp,L"Exportar");AppendMenuW(menu,MF_STRING,ID_HELP,L"Ajuda / ISA");SetMenu(window,menu);
        title=control(L"STATIC",L"ChipLab",SS_LEFT);SendMessageW(title,WM_SETFONT,(WPARAM)heading,TRUE);
        subtitle=control(L"STATIC",L"Laboratório de arquitetura de processadores",SS_LEFT);
        modeLabel=control(L"STATIC",L"Modo",SS_LEFT);
        mode=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,ID_MODE);
        SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)L"CPU didática");
        SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)L"x86-32 • Intel / NASM");
        SendMessageW(mode,CB_SETCURSEL,0,0);
        const wchar_t* labels[]={L"Nome da arquitetura",L"Palavra (bits)",L"Registradores",L"Memória (palavras)",L"Limite de passos"};
        configLabels[0]=control(L"STATIC",labels[0],SS_LEFT);
        name=control(L"EDIT",L"CPU didática",WS_TABSTOP|ES_AUTOHSCROLL,ID_NAME);SendMessageW(name,EM_SETLIMITTEXT,256,0);
        configLabels[1]=control(L"STATIC",labels[1],SS_LEFT);
        bits=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,ID_BITS);
        configLabels[2]=control(L"STATIC",labels[2],SS_LEFT);
        regs=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,ID_REGS);
        configLabels[3]=control(L"STATIC",labels[3],SS_LEFT);
        mem=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,ID_MEMORY);
        configLabels[4]=control(L"STATIC",labels[4],SS_LEFT);
        limit=control(L"EDIT",L"1000",ES_NUMBER|WS_TABSTOP,ID_LIMIT);SendMessageW(limit,EM_SETLIMITTEXT,20,0);
        for(auto s:{L"8",L"16",L"32"})SendMessageW(bits,CB_ADDSTRING,0,(LPARAM)s);
        for(auto s:{L"4",L"8",L"16"})SendMessageW(regs,CB_ADDSTRING,0,(LPARAM)s);
        for(auto s:{L"64",L"128",L"256"})SendMessageW(mem,CB_ADDSTRING,0,(LPARAM)s);
        SendMessageW(bits,CB_SETCURSEL,0,0);SendMessageW(regs,CB_SETCURSEL,1,0);SendMessageW(mem,CB_SETCURSEL,0,0);
        const wchar_t* actions[]={L"Montar",L"Passo",L"Executar",L"Pausar",L"Reiniciar"};
        for(int i=0;i<5;++i) buttons[i]=control(L"BUTTON",actions[i],BS_PUSHBUTTON|WS_TABSTOP,ID_ASSEMBLE+i);
        hint=control(L"STATIC",L"Até 1.000 instruções por execução • valores sem sinal • ISA didática",SS_LEFT);SendMessageW(hint,WM_SETFONT,(WPARAM)smallFont,TRUE);
        editorLabel=control(L"STATIC",L"01  PROGRAMA ASSEMBLY",SS_LEFT);
        editor=control(L"EDIT",DEFAULT_SOURCE,WS_TABSTOP|ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL|ES_AUTOHSCROLL|WS_VSCROLL|WS_HSCROLL,ID_EDITOR);
        // O controle conta unidades UTF-16 e CRLF; o núcleo conta bytes UTF-8 em LF.
        // Espaço adicional evita cortar uma fonte válida antes de sua validação.
        SendMessageW(editor,WM_SETFONT,(WPARAM)mono,TRUE);SendMessageW(editor,EM_SETLIMITTEXT,MAX_SOURCE_LENGTH*2+2,0);
        state=control(L"STATIC",L"",SS_LEFT);
        consoleLabel=control(L"STATIC",L"SAÍDA DO PROGRAMA (int 0x80: write)",SS_LEFT);
        SendMessageW(consoleLabel,WM_SETFONT,(WPARAM)smallFont,TRUE);
        console=control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL);
        SendMessageW(console,WM_SETFONT,(WPARAM)mono,TRUE);
        SendMessageW(console,EM_SETLIMITTEXT,131072,0);
        regsLabel=control(L"STATIC",L"02  REGISTRADORES",SS_LEFT);
        regTable=table({{L"Reg.",58},{L"Decimal",95},{L"Hex",100}});
        memLabel=control(L"STATIC",L"03  MEMÓRIA DE DADOS",SS_LEFT);
        memTable=table({{L"End.",60},{L"Decimal",95},{L"Hex",100}});
        memoryAddress=control(L"EDIT",L"0x1000",ES_AUTOHSCROLL|WS_TABSTOP,ID_MEM_START);
        SendMessageW(memoryAddress,EM_SETLIMITTEXT,20,0);
        memoryView=control(L"BUTTON",L"Ver",BS_PUSHBUTTON|WS_TABSTOP,ID_MEM_VIEW);
        traceLabel=control(L"STATIC",L"04  RASTRO DE EXECUÇÃO",SS_LEFT);
        traceTable=table({{L"Ciclo",65},{L"PC",60},{L"Instrução",290},{L"Próx. PC",90},{L"Alterações",550}});
        status=control(L"STATIC",L"Pronto. Use Passo ou Executar para iniciar.",SS_LEFT);SendMessageW(status,WM_SETFONT,(WPARAM)smallFont,TRUE);
        modified=false; assembleNow(false); modified=false; updateModeControls();layout();
    }
    void layout() {
        if(!editor)return;
        RECT r;GetClientRect(window,&r);int w=r.right,h=r.bottom;
        auto place=[](HWND c,int x,int y,int a,int b){MoveWindow(c,x,y,a,b,TRUE);};
        place(title,24,15,180,40);place(subtitle,220,30,std::max(200,w-620),25);
        place(modeLabel,w-370,31,48,25);place(mode,w-316,25,292,160);
        int widths[]={std::max(180,w-866),120,140,170,150},x=24;
        HWND fields[]={name,bits,regs,mem,limit};
        for(int i=0;i<5;++i){place(configLabels[i],x,91,widths[i],23);place(fields[i],x,117,widths[i],(i>0&&i<4)?180:30);x+=widths[i]+18;}
        for(int i=0;i<5;++i)place(buttons[i],24+i*112,162,102,34);
        place(hint,600,170,w-620,25);
        int split=std::max(440,w*43/100),rightX=split+42,rightW=w-rightX-24;
        int traceY=h-224,contentH=std::max(240,traceY-252);
        place(editorLabel,24,215,split,22);place(editor,24,242,split,contentH);
        place(state,rightX,242,rightW,useX86?88:100);
        int tableY=useX86?450:388, labelY=useX86?419:360;
        place(consoleLabel,rightX,332,rightW,18);place(console,rightX,352,rightW,43);
        place(regsLabel,rightX,labelY,rightW/2-8,24);place(memLabel,rightX+rightW/2+8,labelY,rightW/2-8,24);
        if(useX86) {
            int addressX=rightX+rightW/2+8;
            place(memLabel,addressX,labelY-20,rightW/2-8,20);
            place(memoryAddress,addressX,labelY, std::max(110,rightW/2-70),26);
            place(memoryView,rightX+rightW-54,labelY,54,26);
        }
        place(regTable,rightX,tableY,rightW/2-8,std::max(80,traceY-tableY-10));
        place(memTable,rightX+rightW/2+8,tableY,rightW/2-8,std::max(80,traceY-tableY-10));
        place(traceLabel,24,traceY,w-48,22);place(traceTable,24,traceY+28,w-48,155);
        place(status,24,h-28,w-48,22);InvalidateRect(window,nullptr,TRUE);
    }
    bool x86Mode() const {return useX86;}
    static std::wstring addressHex(uint32_t value,unsigned digits=8) {
        std::wostringstream s;s<<L"0x"<<std::uppercase<<std::hex<<std::setfill(L'0')<<std::setw(digits)<<value;return s.str();
    }
    void updateModeControls() {
        SendMessageW(mode,CB_SETCURSEL,useX86?1:0,0);
        EnableWindow(bits,!running&&!useX86);EnableWindow(regs,!running&&!useX86);EnableWindow(mem,!running&&!useX86);
        for(HWND h:{console,consoleLabel,memoryAddress,memoryView})ShowWindow(h,useX86?SW_SHOW:SW_HIDE);
        setText(configLabels[3],useX86?L"Memória mapeada":L"Memória (palavras)");
        setText(hint,useX86?L"x86-32 • bytes reais • até 1.000 instruções por execução":L"Até 1.000 instruções por execução • valores sem sinal • ISA didática");
        setText(memLabel,useX86?L"03  MEMÓRIA • ENDEREÇO INICIAL":L"03  MEMÓRIA DE DADOS");
        setText(traceLabel,useX86?L"04  RASTRO x86 • BYTES E INSTRUÇÃO DECODIFICADA":L"04  RASTRO DE EXECUÇÃO");
        const wchar_t* captions[]={L"Ciclo",useX86?L"EIP":L"PC",useX86?L"Bytes | Assembly Intel":L"Instrução",useX86?L"Próx. EIP":L"Próx. PC",L"Alterações"};
        const int widths[]={65,useX86?105:60,useX86?430:290,useX86?110:90,550};
        for(int i=0;i<5;++i){LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=(LPWSTR)captions[i];col.cx=widths[i];ListView_SetColumn(traceTable,i,&col);}
        ListView_SetColumnWidth(regTable,0,useX86?74:58);
        ListView_SetColumnWidth(regTable,1,useX86?112:95);
        ListView_SetColumnWidth(regTable,2,useX86?112:100);
        LVCOLUMNW decimalColumn{};decimalColumn.mask=LVCF_TEXT;
        decimalColumn.pszText=(LPWSTR)(useX86?L"Dec.":L"Decimal");
        ListView_SetColumn(memTable,1,&decimalColumn);
        ListView_SetColumnWidth(memTable,0,useX86?112:60);
        ListView_SetColumnWidth(memTable,1,useX86?60:95);
        ListView_SetColumnWidth(memTable,2,useX86?70:100);
        EnableMenuItem(GetMenu(window),ID_VERILOG,MF_BYCOMMAND|((running||useX86)?MF_GRAYED:MF_ENABLED));
        layout();
    }
    void clearViews() {
        ListView_DeleteAllItems(regTable);ListView_DeleteAllItems(memTable);ListView_DeleteAllItems(traceTable);renderedTrace=0;setText(console,L"");
    }
    void setX86Mode(bool enabled,bool replaceExample=false) {
        if(useX86==enabled){updateModeControls();return;}
        stop();
        auto original=textOf(editor);
        if(enabled) {
            didacticSelection={utf8(textOf(name)),std::stoi(textOf(bits)),std::stoi(textOf(regs)),std::stoi(textOf(mem))};
        }
        updating=true;useX86=enabled;
        SendMessageW(mem,CB_RESETCONTENT,0,0);
        if(enabled) {
            SendMessageW(bits,CB_SETCURSEL,2,0);SendMessageW(regs,CB_SETCURSEL,1,0);
            SendMessageW(mem,CB_ADDSTRING,0,(LPARAM)L"2 MiB (bytes)");SendMessageW(mem,CB_SETCURSEL,0,0);
            memoryStart=x86::MEMORY_BASE;setText(memoryAddress,addressHex(memoryStart));
        }else {
            for(auto s:{L"64",L"128",L"256"})SendMessageW(mem,CB_ADDSTRING,0,(LPARAM)s);
            SendMessageW(bits,CB_SETCURSEL,didacticSelection.bits==8?0:didacticSelection.bits==16?1:2,0);
            SendMessageW(regs,CB_SETCURSEL,didacticSelection.registers==4?0:didacticSelection.registers==8?1:2,0);
            SendMessageW(mem,CB_SETCURSEL,didacticSelection.memory_size==64?0:didacticSelection.memory_size==128?1:2,0);
        }
        if(replaceExample&&(original==DEFAULT_SOURCE||original==X86_SOURCE)) {
            setText(editor,enabled?X86_SOURCE:DEFAULT_SOURCE);
            setText(name,enabled?L"x86 Intel / NASM":L"CPU didática");
        }
        updating=false;cpu.reset();x86cpu.reset();clearViews();dirty=true;modified=true;
        setText(state,enabled?L"Modo x86-32 selecionado. Monte o código NASM para inicializar.":L"Modo didático selecionado. Monte o programa para inicializar a CPU.");
        updateModeControls();
        message(L"Modo alterado. Montar inicializa um novo estado; um programa personalizado é preservado no editor.");
    }
    Architecture config() {
        Architecture a; a.name=utf8(textOf(name));
        if(useX86){a.bits=32;a.registers=8;a.memory_size=64;a.validate();return a;}
        a.bits=std::stoi(textOf(bits));a.registers=std::stoi(textOf(regs));a.memory_size=std::stoi(textOf(mem));a.validate();return a;
    }
    std::string sourceText() {
        auto s=utf8(textOf(editor));std::string result;
        for(size_t i=0;i<s.size();++i) {
            if(s[i]=='\r') {if(i+1<s.size()&&s[i+1]=='\n')++i;result+='\n';}
            else result+=s[i];
        }
        return result;
    }
    bool needsAssembly() {
        if(useX86) {
            if(dirty||!x86cpu)return true;
            bool changed=config().name!=assembledName||sourceText()!=x86cpu->program.source;
            if(changed){dirty=true;modified=true;}
            return changed;
        }
        if(dirty||!cpu)return true;
        auto a=config();
        bool changed=a.name!=architecture.name||a.bits!=architecture.bits||a.registers!=architecture.registers||a.memory_size!=architecture.memory_size||sourceText()!=cpu->program.source;
        if(changed){dirty=true;modified=true;}
        return changed;
    }
    void message(const std::wstring& s) {setText(status,s);}
    void error(const std::exception& e,bool notify=true) {stop();std::wstring s=diagnosticText(e);message(L"Falha: "+s);if(notify)MessageBoxW(window,s.c_str(),L"ChipLab • validação",MB_OK|MB_ICONWARNING);}
    bool confirmDiscard() {
        return !modified || MessageBoxW(window,L"Descartar as alterações ainda não salvas?",L"ChipLab",MB_YESNO|MB_ICONQUESTION)==IDYES;
    }
    void stop() {running=false;KillTimer(window,1);enableControls();refresh();}
    void enableControls() {
        for(HWND h:{name,bits,regs,mem,limit,mode,memoryAddress,memoryView})EnableWindow(h,!running);
        SendMessageW(editor,EM_SETREADONLY,running,0);
        for(int i=0;i<5;++i)EnableWindow(buttons[i],i==3?running:!running);
        for(int id:{ID_NEW,ID_OPEN,ID_SAVE,ID_ASM,ID_CSV,ID_VERILOG})EnableMenuItem(GetMenu(window),id,MF_BYCOMMAND|(running?MF_GRAYED:MF_ENABLED));
        if(useX86){for(HWND h:{bits,regs,mem})EnableWindow(h,FALSE);EnableMenuItem(GetMenu(window),ID_VERILOG,MF_BYCOMMAND|MF_GRAYED);}
    }
    bool assembleNow(bool notify=true) {
        stop();Architecture a=config();
        if(useX86) {
            auto p=x86::assemble(sourceText(),x86::runtime_directory());
            auto next=std::make_unique<x86::CPU>(p,x86::runtime_directory());
            assembledName=a.name;architecture=a;x86cpu=std::move(next);dirty=false;
            ListView_DeleteAllItems(traceTable);renderedTrace=0;refresh();
            if(notify)message(L"NASM: "+std::to_wstring(p.machine_code.size())+L" bytes montados. EIP inicial "+addressHex(p.entry)+L". Estado inicial reiniciado.");
            return true;
        }
        auto p=assemble(sourceText(),a);
        auto next=std::make_unique<CPU>(a,p);architecture=a;cpu=std::move(next);dirty=false;
        ListView_DeleteAllItems(traceTable);renderedTrace=0;refresh();
        if(notify)message(L"Montagem concluída: "+std::to_wstring(p.instructions.size())+L" instruções. Estado inicial zerado.");
        return true;
    }
    std::wstring hex(uint32_t value) {
        std::wostringstream s;s<<L"0x"<<std::uppercase<<std::hex<<std::setfill(L'0')<<std::setw(architecture.bits/4)<<value;return s.str();
    }
    std::wstring changes(const TraceEntry& e) {
        std::wstring s;for(auto& [i,v]:e.register_changes)s+=L"R"+std::to_wstring(i)+L"="+std::to_wstring(v)+L"  ";
        for(auto& [i,v]:e.memory_changes)s+=L"M["+std::to_wstring(i)+L"]="+std::to_wstring(v)+L"  ";
        if(e.halted)s+=L"HALT";
        return s.empty()?L"—":s;
    }
    std::wstring changes(const x86::TraceEntry& e) {
        std::wstring s;for(auto& [reg,value]:e.register_changes)s+=wide(reg)+L"="+addressHex(value)+L"  ";
        for(auto& [address,value]:e.memory_changes)s+=L"["+addressHex(address)+L"]="+addressHex(value,2)+L"  ";
        if(e.halted)s+=L"FIM";
        return s.empty()?L"—":s;
    }
    static std::wstring outputText(const std::string& value) {
        // write(1, ...) entrega bytes; uma sequência inválida não pode impedir
        // a inspeção de registradores. UTF-8 válido permanece fiel ao programa.
        int n=MultiByteToWideChar(CP_UTF8,0,value.data(),(int)value.size(),nullptr,0);
        std::wstring out(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,value.data(),(int)value.size(),out.data(),n);
        std::wstring normalized;for(size_t i=0;i<out.size();++i){if(out[i]==L'\r'){if(i+1<out.size()&&out[i+1]==L'\n')++i;normalized+=L"\r\n";}else if(out[i]==L'\n')normalized+=L"\r\n";else if(out[i]==0)normalized+=L"\\0";else normalized+=out[i];}return normalized;
    }
    void refreshX86() {
        if(!x86cpu)return;
        const auto s=x86cpu->snapshot();
        std::wstring flags;
        for(auto flag:std::initializer_list<std::pair<const wchar_t*,unsigned>>{{L"CF",0},{L"PF",2},{L"AF",4},{L"ZF",6},{L"SF",7},{L"DF",10},{L"OF",11}})
            flags+=std::wstring(flag.first)+L"="+((s.eflags&(1u<<flag.second))?L"1 ":L"0 ");
        std::wstring next;
        if(x86cpu->halted)next=wide(x86cpu->halt_reason);
        else {try{next=wide(x86cpu->nextInstruction());}catch(const std::exception&){next=L"Endereço sem instrução decodificável; verifique EIP e os bytes.";}}
        setText(state,L"x86-32 Intel/NASM • memória em bytes • "+std::wstring(x86cpu->halted?L"CONCLUÍDO":running?L"EXECUTANDO":L"PRONTO")+
            L"\r\nEIP: "+addressHex(s.eip)+L"     Instruções: "+std::to_wstring(x86cpu->cycles)+L"\r\n"+flags+L"\r\nPróxima / parada: "+next);
        const wchar_t* names[]={L"EAX",L"EBX",L"ECX",L"EDX",L"ESI",L"EDI",L"EBP",L"ESP"};
        SendMessageW(regTable,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(regTable);
        for(unsigned i=0;i<8;++i)row(regTable,(int)i,{names[i],std::to_wstring(s.regs[i]),addressHex(s.regs[i])});
        row(regTable,8,{L"EIP",std::to_wstring(s.eip),addressHex(s.eip)});
        row(regTable,9,{L"EFLAGS",std::to_wstring(s.eflags),addressHex(s.eflags)});
        SendMessageW(regTable,WM_SETREDRAW,TRUE,0);InvalidateRect(regTable,nullptr,FALSE);
        const auto mapEnd=uint64_t(x86::MEMORY_BASE)+x86::MEMORY_SIZE;
        if(memoryStart<x86::MEMORY_BASE||memoryStart>=mapEnd)memoryStart=x86::MEMORY_BASE;
        const auto memory=x86cpu->readMemory(memoryStart,(size_t)std::min<uint64_t>(256,mapEnd-memoryStart));
        int top=ListView_GetTopIndex(memTable);
        SendMessageW(memTable,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(memTable);
        for(size_t i=0;i<memory.size();++i)row(memTable,(int)i,{addressHex(memoryStart+(uint32_t)i),std::to_wstring(memory[i]),addressHex(memory[i],2)});
        SendMessageW(memTable,WM_SETREDRAW,TRUE,0);InvalidateRect(memTable,nullptr,FALSE);
        if(top>0&&top<(int)memory.size())ListView_EnsureVisible(memTable,top,FALSE);
        setText(console,outputText(x86cpu->output));
        for(;renderedTrace<x86cpu->trace.size();++renderedTrace){auto& e=x86cpu->trace[renderedTrace];row(traceTable,(int)renderedTrace,{std::to_wstring(e.cycle),addressHex(e.pc),wide(e.bytes)+L" | "+wide(e.instruction),addressHex(e.next_pc),changes(e)});}
        if(renderedTrace)ListView_EnsureVisible(traceTable,(int)renderedTrace-1,FALSE);
    }
    void refresh() {
        if(useX86){refreshX86();return;}
        if(!cpu)return;
        std::wstring next=cpu->halted?L"HALT":cpu->pc<cpu->program.instructions.size()?wide(cpu->program.instructions[cpu->pc].text()):L"PC fora do programa";
        setText(state,L"CPU  "+std::to_wstring(architecture.bits)+L" bits  •  "+std::to_wstring(architecture.registers)+L" registradores  •  "+std::to_wstring(architecture.memory_size)+L" palavras\r\n\r\nPC: "+std::to_wstring(cpu->pc)+L"     Ciclos: "+std::to_wstring(cpu->cycles)+L"     "+(cpu->halted?L"CONCLUÍDO":running?L"EXECUTANDO":L"PRONTO")+L"\r\nPróxima: "+next);
        SendMessageW(regTable,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(regTable);
        for(size_t i=0;i<cpu->registers.size();++i)row(regTable,(int)i,{L"R"+std::to_wstring(i),std::to_wstring(cpu->registers[i]),hex(cpu->registers[i])});
        SendMessageW(regTable,WM_SETREDRAW,TRUE,0);InvalidateRect(regTable,nullptr,FALSE);
        int top=ListView_GetTopIndex(memTable);
        if(ListView_GetItemCount(memTable)!=(int)cpu->memory.size()){ListView_DeleteAllItems(memTable);for(size_t i=0;i<cpu->memory.size();++i)row(memTable,(int)i,{std::to_wstring(i),std::to_wstring(cpu->memory[i]),hex(cpu->memory[i])});}
        else for(size_t i=0;i<cpu->memory.size();++i){auto v=std::to_wstring(cpu->memory[i]),hx=hex(cpu->memory[i]);ListView_SetItemText(memTable,(int)i,1,v.data());ListView_SetItemText(memTable,(int)i,2,hx.data());}
        if(top>0)ListView_EnsureVisible(memTable,top,FALSE);
        for(;renderedTrace<cpu->trace.size();++renderedTrace){auto& e=cpu->trace[renderedTrace];row(traceTable,(int)renderedTrace,{std::to_wstring(e.cycle),std::to_wstring(e.pc),wide(e.instruction),std::to_wstring(e.next_pc),changes(e)});}
        if(renderedTrace)ListView_EnsureVisible(traceTable,(int)renderedTrace-1,FALSE);
    }
    void step() {
        if(needsAssembly())assembleNow(false);
        if(useX86) {
            if(x86cpu->halted){message(L"Programa concluído. Use Reiniciar para executar novamente.");return;}
            auto e=x86cpu->step();refresh();message(x86cpu->halted?L"Execução x86 concluída: "+wide(x86cpu->halt_reason):L"Executada: "+wide(e.instruction));return;
        }
        if(cpu->halted){message(L"Programa concluído. Use Reiniciar para executar novamente.");return;}
        auto e=cpu->step();refresh();message(cpu->halted?L"HALT: execução concluída.":L"Executada: "+wide(e.instruction));
    }
    void run() {
        auto requested=detail::number(utf8(textOf(limit)),"Limite de passos");
        if(requested<1||requested>1000)throw std::runtime_error("Limite de passos: informe um inteiro entre 1 e 1000.");
        if(needsAssembly())assembleNow(false);
        if(useX86?x86cpu->halted:cpu->halted){message(L"Programa concluído. Use Reiniciar para executar novamente.");return;}
        running=true;remaining=(size_t)requested;enableControls();SetTimer(window,1,15,nullptr);refresh();message(L"Executando com limite de "+std::to_wstring(remaining)+L" instruções. Pausar preserva o estado.");
    }
    void tick() {
        if(!running)return;
        try {
            for(int i=0;i<20 && remaining && !(useX86?x86cpu->halted:cpu->halted);++i,--remaining){if(useX86)x86cpu->step();else cpu->step();}
            if(useX86?x86cpu->halted:cpu->halted){stop();message(useX86?L"Execução x86 concluída: "+wide(x86cpu->halt_reason):L"HALT: programa concluído. Memória e registradores disponíveis para inspeção.");}
            else if(!remaining){stop();message(L"Limite de passos atingido. Estado preservado; execute novamente para continuar.");}
            refresh();
        }catch(const std::exception& e){refresh();error(e);}
    }
    std::wstring dialog(bool save,const wchar_t* filter,const wchar_t* ext,const wchar_t* suggested) {
        wchar_t file[32768]{};if(save)wcsncpy_s(file,suggested,_TRUNCATE);
        OPENFILENAMEW o{};o.lStructSize=sizeof(o);o.hwndOwner=window;o.lpstrFilter=filter;o.lpstrFile=file;o.nMaxFile=32768;o.lpstrDefExt=ext;
        o.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        if(save?GetSaveFileNameW(&o):GetOpenFileNameW(&o))return file;
        return {};
    }
    void writeFile(const std::wstring& path,const std::string& content) {
        detail::write_file_atomic(fs::path(path),content);
    }
    std::string readFile(const std::wstring& path) {
        std::ifstream in(fs::path(path),std::ios::binary);if(!in)throw std::runtime_error("Não foi possível abrir o arquivo.");
        std::string content(MAX_PROJECT_BYTES+1,'\0');in.read(content.data(),(std::streamsize)content.size());
        if(in.bad())throw std::runtime_error("Falha ao ler o arquivo.");
        content.resize((size_t)in.gcount());
        if(content.size()>MAX_PROJECT_BYTES)throw std::runtime_error("Arquivo excede 1 MiB.");
        return content;
    }
    void loadValues(const Architecture& a,const std::string& source) {
        a.validate();detail::check_source(source);
        auto nextName=wide(a.name),nextSource=windowsLines(source);
        setX86Mode(false);
        updating=true;setText(name,nextName);setText(editor,nextSource);
        SendMessageW(bits,CB_SETCURSEL,a.bits==8?0:a.bits==16?1:2,0);SendMessageW(regs,CB_SETCURSEL,a.registers==4?0:a.registers==8?1:2,0);
        SendMessageW(mem,CB_SETCURSEL,a.memory_size==64?0:a.memory_size==128?1:2,0);updating=false;
        stop();cpu.reset();x86cpu.reset();clearViews();
        dirty=true;setText(state,L"Projeto carregado. Monte o programa para inicializar a CPU.");
    }
    void loadX86Values(const std::string& projectName,const std::string& source) {
        Architecture{projectName,32,8,64}.validate();detail::check_source(source);
        auto nextName=wide(projectName),nextSource=windowsLines(source);
        setX86Mode(true);stop();updating=true;setText(name,nextName);setText(editor,nextSource);updating=false;
        cpu.reset();x86cpu.reset();clearViews();dirty=true;
        memoryStart=x86::MEMORY_BASE;setText(memoryAddress,addressHex(memoryStart));
        setText(state,L"Projeto x86-32 carregado. Monte o programa NASM para inicializar.");
    }
    void viewMemory() {
        auto requested=detail::number(utf8(textOf(memoryAddress)),"Endereço inicial da memória");
        if(requested<x86::MEMORY_BASE||uint64_t(requested)>=uint64_t(x86::MEMORY_BASE)+x86::MEMORY_SIZE)
            throw std::runtime_error("Endereço fora da memória x86 mapeada: 0x1000 a 0x200FFF.");
        memoryStart=(uint32_t)requested;setText(memoryAddress,addressHex(memoryStart));refresh();
        message(L"Janela de até 256 bytes a partir de "+addressHex(memoryStart)+L". Os endereços são de bytes; dword usa 4 bytes em little-endian.");
    }
    static std::string csvQuote(const std::string& s) {std::string out="\"";for(char c:s){if(c=='\"')out+='\"';out+=c;}return out+'\"';}
    std::string traceCsv() {
        if(useX86) {
            if(!x86cpu||x86cpu->trace.empty())throw std::runtime_error("Execute ao menos uma instrução antes de exportar o rastro.");
            std::ostringstream s;s<<"\xEF\xBB\xBF"<<"instrucao_numero,eip,bytes,instrucao,proximo_eip,alteracoes,finalizado\r\n";
            for(auto& e:x86cpu->trace)s<<e.cycle<<','<<utf8(addressHex(e.pc))<<','<<csvQuote(e.bytes)<<','<<csvQuote(e.instruction)<<','<<utf8(addressHex(e.next_pc))<<','<<csvQuote(utf8(changes(e)))<<','<<(e.halted?1:0)<<"\r\n";
            return s.str();
        }
        if(!cpu||cpu->trace.empty())throw std::runtime_error("Execute ao menos uma instrução antes de exportar o rastro.");
        std::ostringstream s;s<<"\xEF\xBB\xBF"<<"ciclo,pc,instrucao,proximo_pc,alteracoes,halt\r\n";
        for(auto& e:cpu->trace)s<<e.cycle<<','<<e.pc<<','<<csvQuote(e.instruction)<<','<<e.next_pc<<','<<csvQuote(utf8(changes(e)))<<','<<(e.halted?1:0)<<"\r\n";
        return s.str();
    }
    void command(int id) {
        if(running && id!=ID_PAUSE && id!=ID_HELP && id!=ID_EXIT)return;
        try {
            switch(id) {
            case ID_MEM_VIEW:if(useX86)viewMemory();break;
            case ID_ASSEMBLE:assembleNow();break;
            case ID_STEP:step();break;
            case ID_RUN:run();break;
            case ID_PAUSE:stop();refresh();message(L"Execução pausada. Estado preservado.");break;
            case ID_RESET:assembleNow(false);message(useX86?L"CPU x86 reiniciada: código recarregado, registradores e rastro iniciais restaurados.":L"CPU reiniciada: registradores, memória e rastro zerados.");break;
            case ID_NEW:if(confirmDiscard()){if(useX86)loadX86Values("x86 Intel / NASM",utf8(X86_SOURCE));else{Architecture a;loadValues(a,utf8(DEFAULT_SOURCE));}projectPath.clear();modified=false;assembleNow();}break;
            case ID_OPEN:{if(!confirmDiscard())break;auto p=dialog(false,L"Projetos ChipLab (*.json)\0*.json\0\0",L"json",L"");if(p.empty())break;
                auto payload=readFile(p);
                if(x86::is_project(payload)){auto [n,s]=x86::parse_project(payload);loadX86Values(n,s);}
                else {auto [a,s]=load_project(fs::path(p));wide(s);wide(a.name);loadValues(a,s);}
                projectPath=p;modified=false;message(L"Projeto aberto. Modo, configuração e fonte recuperados; execução ainda não iniciada.");break;}
            case ID_SAVE:{auto a=config();auto p=dialog(true,L"Projetos ChipLab (*.json)\0*.json\0\0",L"json",L"projeto.chiplab.json");if(p.empty())break;
                if(useX86)writeFile(p,x86::project_json(a.name,sourceText()));else save_project(fs::path(p),a,sourceText());
                projectPath=p;modified=false;message(L"Projeto salvo: modo, configuração e programa. O estado da execução não é salvo.");break;}
            case ID_ASM:{if(!confirmDiscard())break;auto p=dialog(false,L"Assembly (*.asm)\0*.asm\0Texto (*.txt)\0*.txt\0\0",L"asm",L"");if(p.empty())break;
                auto source=readFile(p);detail::check_source(source);setText(editor,windowsLines(source));dirty=true;modified=true;message(L"Programa importado. Clique em Montar para validar.");break;}
            case ID_CSV:{auto csv=traceCsv();
                auto p=dialog(true,L"CSV (*.csv)\0*.csv\0\0",L"csv",L"rastro.csv");if(p.empty())break;
                writeFile(p,csv);message(L"Rastro CSV exportado com "+std::to_wstring(useX86?x86cpu->trace.size():cpu->trace.size())+L" instruções.");break;}
            case ID_VERILOG:{if(useX86)throw std::runtime_error("A exportação Verilog se aplica somente à ULA didática; não gera um processador x86.");auto a=config();auto p=dialog(true,L"Verilog (*.v)\0*.v\0\0",L"v",L"chiplab_alu.v");if(p.empty())break;writeFile(p,export_verilog(a));message(L"ULA combinacional exportada. Este arquivo não contém uma CPU completa.");break;}
            case ID_HELP:if(useX86){MessageBoxW(window,
                L"CHIPLAB • x86-32 INTEL / NASM\n\n"
                L"Código NASM de 32 bits montado em bytes reais e executado pelo Unicorn.\nO rastro decodifica esses bytes com sintaxe Intel (Capstone).\nBITS 32 é opcional. A origem fixa 0x1000 é definida pelo ChipLab; remova ORG do fonte.\nEntrada: _start, start ou main; sem esses rótulos, início do binário.\n\n"
                L"Memória: 2 MiB a partir de 0x1000; endereço de byte.\nRegistradores EAX, EBX, ECX, EDX, ESI, EDI, EBP e ESP; EIP e EFLAGS.\nSubregistradores AL/AH/AX e demais seguem as regras reais do x86.\n\n"
                L"HLT conclui. int 0x80 oferece write (EAX=4, EBX=1/2, ECX=buffer, EDX=tamanho) e exit (EAX=1).\nA saída write aparece no painel do programa.\nNão há um sistema operacional, APIs do Windows ou execução de ELF/EXE.\nCódigo de 16/64 bits exige adaptação para este modo de 32 bits.\n\n"
                L"Montar/Reiniciar recarregam a memória e apagam o rastro.\nExecutar respeita o limite de até 1.000 instruções; o estado é preservado no limite.\nNa memória, digite um endereço hexadecimal e use Ver.\nExemplo: ESP inspeciona a pilha; o endereço da instrução mov [endereço], eax inspeciona o resultado.\nContagem de instruções não representa latência física.\n\n"
                L"Projetos JSON guardam o modo e o fonte. CSV guarda bytes, EIP e alterações.\nULA Verilog disponível no modo didático.",L"Ajuda do ChipLab x86-32",MB_OK|MB_ICONINFORMATION);break;}
                MessageBoxW(window,
                L"CHIPLAB • ISA DIDÁTICA\n\n"
                L"MOV Rd, imediato | MOV Rd, Rs\nADD / SUB / AND / OR / XOR Rd, Ra, Rb\nADDI Rd, Ra, imediato\nLOAD Rd, endereço | STORE Rs, endereço\nJMP rótulo | JZ / JNZ Rn, rótulo\nHALT\n\n"
                L"Rótulos terminam com dois-pontos. Comentários: ; ou #.\nImediatos: decimal ou hexadecimal (0x).\nAritmética modular sem sinal: 8, 16 ou 32 bits.\nR0 é um registrador comum. HALT é explícito.\nMemória de dados e instruções são separadas.\n\n"
                L"Montar/Reiniciar apagam o estado e o rastro.\nEditar exige nova montagem ao executar.\nExecutar avança até HALT, erro, pausa ou 1.000 instruções.\nCada instrução equivale a um ciclo didático, sem latência física.\n\n"
                L"Exportação Verilog: somente ULA combinacional.\nEste simulador não implementa a ISA RISC-V.\n\nUnijorge • Engenharia da Computação • Grupo 2 • 2026",
                L"Ajuda do ChipLab",MB_OK|MB_ICONINFORMATION);break;
            case ID_EXIT:SendMessageW(window,WM_CLOSE,0,0);break;
            }
        }catch(const std::exception& e){error(e);}
    }
};

LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* app=(App*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_CREATE) {app=(App*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)app);try{app->initialize(hwnd);}catch(const std::exception& e){MessageBoxW(hwnd,diagnosticText(e).c_str(),L"Erro de inicialização",MB_ICONERROR);return -1;}return 0;}
    if(!app)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg) {
    case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize={1120,850};return 0;
    case WM_SIZE:app->layout();return 0;
    case WM_TIMER:app->tick();return 0;
    case WM_COMMAND: {
        int id=LOWORD(wp),event=HIWORD(wp);
        if(!app->updating&&id==ID_MODE&&event==CBN_SELCHANGE) {
            try{app->setX86Mode(SendMessageW(app->mode,CB_GETCURSEL,0,0)==1,true);}catch(const std::exception& e){SendMessageW(app->mode,CB_SETCURSEL,app->x86Mode()?1:0,0);app->error(e);}
        }else if(!app->updating && (((id==ID_EDITOR||id==ID_NAME)&&event==EN_CHANGE) || ((id==ID_BITS||id==ID_REGS||id==ID_MEMORY)&&event==CBN_SELCHANGE))) {
            app->dirty=true;app->modified=true;app->message(L"Alterações pendentes. Montar/Passo/Executar inicializam um novo estado antes de executar.");
        }else if(event==BN_CLICKED || !lp)app->command(id);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc=(HDC)wp;HWND child=(HWND)lp;SetBkMode(dc,TRANSPARENT);
        if(child==app->title||child==app->subtitle||child==app->modeLabel){SetTextColor(dc,RGB(245,250,255));SetBkColor(dc,RGB(22,37,60));static HBRUSH dark=CreateSolidBrush(RGB(22,37,60));return (LRESULT)dark;}
        SetTextColor(dc,RGB(35,52,74));SetBkColor(dc,RGB(244,246,250));return (LRESULT)app->background;
    }
    case WM_CTLCOLOREDIT:SetTextColor((HDC)wp,RGB(30,47,70));SetBkColor((HDC)wp,RGB(255,255,255));return (LRESULT)app->white;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);FillRect(dc,&r,app->background);r.bottom=77;HBRUSH b=CreateSolidBrush(RGB(22,37,60));FillRect(dc,&r,b);DeleteObject(b);EndPaint(hwnd,&ps);return 0;}
    case WM_CLOSE:if(app->confirmDiscard())DestroyWindow(hwnd);return 0;
    case WM_DESTROY:app->stop();PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    SetProcessDPIAware();INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=instance;wc.lpszClassName=L"ChipLabDesktop";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassW(&wc);
    App app;HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"ChipLab | Arquitetura de processadores",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1340,930,nullptr,nullptr,instance,&app);
    if(!hwnd)return 1;
    ShowWindow(hwnd,show);UpdateWindow(hwnd);MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    return (int)msg.wParam;
}
