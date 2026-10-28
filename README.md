# ChipLab 1.1.0

## Novo modo x86 Intel/NASM

No seletor **Modo**, escolha **x86-32 • Intel / NASM**. A versão 1.1.0 usa NASM para gerar bytes reais, Unicorn para executá-los em memória emulada e Capstone para exibir a instrução decodificada. O modo **CPU didática** e os projetos antigos continuam disponíveis.

Extraia a pasta inteira antes de abrir `ChipLab.exe`, incluindo **runtime**. Não é preciso instalar Python nem um montador separado. Use **Arquivo > Novo exemplo** para recuperar o exemplo do modo escolhido; um código personalizado já digitado é preservado ao trocar de modo.

Cole este exemplo no modo x86 e use **Montar**, **Passo** ou **Executar**:

```asm
bits 32
section .text
global _start
_start:
    mov eax, 5
    add eax, 3
    mov [0x110000], eax
    hlt
```

Resultado: **EAX = 8**, após quatro instruções. No campo de endereço inicial da memória, digite **0x110000** e clique **Ver**: os bytes são **08 00 00 00**, em little-endian.

O painel apresenta EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP, EIP e EFLAGS, com flags CF/PF/AF/ZF/SF/DF/OF. AL, AH, AX e os demais subregistradores seguem a semântica x86. Os valores decimais são mostrados sem sinal; saltos assinados usam as flags reais. O rastro registra bytes, instrução Intel, endereços e alterações.

A RAM emulada tem **2 MiB**, de **0x1000 a 0x200FFF**, endereçada por **bytes**. A janela mostra até 256 bytes a partir do endereço informado. ESP inicia em **0x200FF0**. O contador mede instruções concluídas, sem representar latência física.

São aceitos programas NASM de **32 bits** com rótulos, `.text`, `.data`, `.bss`, constantes, macros, pilha, chamadas e acesso à memória, conforme as instruções executáveis pelo emulador. A entrada usa `_start`, `start` ou `main`, nessa prioridade; sem esses símbolos, começa no início do binário. Termine com **HLT** ou com a saída virtual **int 0x80**.

A origem fixa **0x1000** é definida pelo ChipLab: **remova ORG do fonte**. Código de 16/64 bits, includes externos, INCBIN, MAP do usuário e endereços de seção personalizados são rejeitados. O ambiente não inclui sistema operacional completo, carregador ELF/EXE ou APIs do Windows. Implementa **int 0x80/sys_write** (EAX=4, EBX=1/2, ECX=endereço, EDX=tamanho) e **sys_exit** (EAX=1); a saída aparece no painel. Outras chamadas de sistema e portas de hardware geram erro controlado.

O limite da fonte é 100000 bytes, o binário tem até 1 MiB, a saída até 64 KiB e o histórico até 100000 instruções por montagem. Executar avança de 1 a 1000 instruções por lote. Cada uma das duas fases do NASM tem limite de 5 segundos. Instruções REP têm limite de tempo e escrita, com restauração do estado em caso de falha.

Projetos x86 usam JSON **versão 2** com modo, nome e fonte. Ao abrir, o editor recupera o código e aguarda montagem; não restaura uma execução anterior. A exportação CSV contém o rastro x86. A exportação Verilog fica restrita à ULA do modo didático.

Experimente os seis programas em **samples/x86**: `soma.asm`, `flags.asm`, `memoria.asm`, `pilha.asm`, `macros.asm` e `ola.asm`. Consulte o [guia de Assembly x86](documentacao/Guia_Assembly_x86.md), o [adendo da SRS](documentacao/SRS_Adendo_x86_1.1.0.md) e o [relatório de validação 1.1.0](documentacao/Relatorio_Validacao_x86_1.1.0.md). Os relatórios PDF/DOCX originais permanecem como registro histórico da primeira versão.


Aplicativo desktop acadêmico para experimentar uma arquitetura de processador didático. A interface nativa reúne programa, registradores, memória e estado da execução. O núcleo foi escrito em C++17; a janela utiliza a API Win32.

A versão 1.0.1 corrige cortes de texto no editor, tratamento de Unicode, abertura de arquivos UTF-8 com BOM, indicação de execução após parada e compilação com MSVC. O registro reproduzível está em `documentacao/Relatorio_Testes_Correcoes.md`; os logs ficam em `evidencias/correcoes_1.0.1`.

## Abrir

1. Extraia a pasta inteira, caso tenha recebido o projeto em um ZIP.
2. No Windows 10 ou 11 de 64 bits, abra **ChipLab.exe**.
3. Clique em **Montar**, depois em **Passo** para observar cada instrução, ou em **Executar** para executar o programa.

O executável fornecido funciona localmente e dispensa instalação de Python, navegador e conexão com a internet. Mantenha a pasta inteira, incluindo runtime, junto do executável. Mantenha o projeto disponível para a apresentação e acompanhamento da atividade até **16 de dezembro de 2026**.

## Experimento inicial do modo didático

A configuração inicial usa 8 registradores de 8 bits e 64 posições de memória. O programa de exemplo soma os números de 1 a 5 e grava **15** na posição **0** da memória. A execução completa contabiliza **20 instruções**; ao final, R0 = 0, R1 = 15, R2 = 15 e PC = 8.

- **Montar** verifica o programa e prepara sua execução.
- **Passo** executa uma instrução e atualiza o estado visível.
- **Executar** avança o programa automaticamente; **Pausar** interrompe esse avanço.
- **Reiniciar** retorna ao estado inicial do programa montado.

O campo **Limite de passos** aceita de **1 a 1.000 instruções** por acionamento, com padrão **1.000**, para interromper programas que não terminam. Ao atingir o limite, o estado é preservado e outro acionamento de Executar pode continuar o programa. Os exemplos em `samples` permitem explorar soma, operações lógicas e transbordamento.

## Configurar a CPU didática

A arquitetura aceita largura de dados de **8, 16 ou 32 bits**, **4, 8 ou 16 registradores**, e **64, 128 ou 256 posições de memória**. Cada posição guarda uma palavra da largura selecionada. Depois de alterar a configuração ou o código, use **Montar** para iniciar uma nova execução.

Use um comando por linha e separe os operandos com vírgulas. Rótulos terminam em `:`; comentários começam com `;` ou `#`. Todos os registradores, incluindo R0, podem ser escritos. O PC indica o índice da instrução. A aritmética mantém os bits menos significativos: em 8 bits, 255 + 1 resulta em 0.

| Forma | Efeito |
| --- | --- |
| `MOV Rd, valor` ou `MOV Rd, Rs` | Carrega constante ou copia registrador. |
| `ADD Rd, Ra, Rb` / `SUB Rd, Ra, Rb` | Soma ou subtrai registradores. |
| `ADDI Rd, Ra, valor` | Soma uma constante. |
| `AND Rd, Ra, Rb` / `OR Rd, Ra, Rb` / `XOR Rd, Ra, Rb` | Opera bit a bit. |
| `LOAD Rd, endereco` | Lê uma posição de memória. |
| `STORE Rs, endereco` | Grava uma posição de memória. |
| `JMP rotulo` | Salta para uma instrução. |
| `JZ Rs, rotulo` / `JNZ Rs, rotulo` | Salta se o registrador for zero / diferente de zero. |
| `HALT` | Encerra a execução. |

## Projetos e saídas

O menu de arquivo permite abrir um programa `.asm` ou abrir e salvar projetos `.chiplab.json`. O projeto guarda a configuração e o código, com esquema 1 no modo didático e esquema 2 no modo x86. Abrir o projeto recupera também o modo. O editor normaliza as quebras de linha para **LF** ao entregar a fonte ao núcleo e ao salvar. O salvamento grava um arquivo temporário completo antes de substituir o projeto de destino. Ao abrir um projeto, monte novamente o código para iniciar a simulação.

Arquivos Assembly e JSON podem ter um BOM UTF-8 inicial. O limite da fonte é de 100.000 bytes UTF-8 após normalização das quebras de linha no editor; os limites são validados sem cortar silenciosamente entradas válidas. Nomes de arquitetura aceitam até 60 pontos Unicode, rejeitando caracteres de controle e nomes compostos apenas por espaços.

Pelo menu de exportação, o histórico pode ser salvo como CSV em UTF-8 e a ALU como Verilog. A exportação Verilog gera **somente uma ALU parametrizada**, conforme a largura de dados selecionada.

A ISA do modo didático é própria; o modo x86-32 usa NASM e CPU emulada. Nenhum modo implementa RISC-V, síntese de uma CPU completa, cache, pipeline, temporização elétrica ou fabricação de chips. Cada instrução concluída corresponde a um passo funcional e incrementa o contador apresentado como ciclo; esse valor não representa a latência de um processador físico.

## Código e documentação

- `src/main.cpp`: interface nativa Windows.
- `src/core.hpp`: montagem e simulação didáticas.
- `src/x86.hpp`: montagem NASM, emulação, flags e projetos x86.
- `runtime`: NASM e bibliotecas nativas incluídas.
- `third_party`: cabeçalhos, procedência, licenças e fonte do Unicorn.
- `tests/test_x86.cpp` e `tests/test_gui_x86.cpp`: CPU e interface x86.
- `tests/test_core.cpp`: testes automatizados do núcleo, sem biblioteca externa de testes.
- `tests/test_gui_win32.cpp`: integração com controles reais da interface em uma janela oculta.
- `tests/test_gui_extended.cpp`: regressões de colagem, Unicode, execução e arquivos na interface.
- `tests/test_cpu_extended.cpp`: limites, arquitetura e comparação com um oráculo independente.
- `tests/test_project_extended.cpp`: persistência, entradas inválidas e 12.000 mutações de JSON.
- `samples`: programas para experimentação.
- `documentacao`: requisitos, fontes públicas e material da primeira etapa.

As fontes de requisitos e a comparação entre duas propostas estão em `documentacao/fontes_requisitos.txt`. Os relatos públicos inspiram a proposta; as adaptações acadêmicas e as decisões de implementação estão identificadas separadamente.

## Recompilar e testar

Para usar o aplicativo pronto, basta abrir o executável. Para recompilar, instale um compilador **C++17**: GCC para Windows (MinGW-w64/UCRT64) ou Visual Studio com as ferramentas de desenvolvimento C++. CMake é uma alternativa para configurar a compilação.

Abra o terminal nesta pasta e execute:

```bat
COMPILAR.cmd
TESTAR.cmd
```

Os scripts procuram `g++.exe` no PATH e em `C:\msys64\ucrt64\bin`. Se não o encontrarem, usam CMake e o compilador configurado no ambiente. `COMPILAR.cmd` produz `ChipLab.exe` nesta pasta. `TESTAR.cmd` compila e executa sete suítes e retorna código de saída diferente de zero em caso de falha. Ele aceita uma pasta opcional para os arquivos de compilação: `TESTAR.cmd "C:\pasta de testes"`. No uso padrão, os arquivos de teste ficam em `build`; as saídas geradas ficam dentro dessa pasta e não substituem as evidências originais do projeto.

A bateria da versão **1.1.0** passou em **28/10/2026** com **GCC 13.2** e **MSVC 19.51**: **sete suítes e 13.878 verificações por compilador**, incluindo 59 do backend x86 e 26 da interface x86. Os seis exemplos NASM do pacote também foram executados. Os resultados, limites e links para os logs estão no [relatório de validação 1.1.0](documentacao/Relatorio_Validacao_x86_1.1.0.md). O total inclui entradas JSON geradas e não representa 13.878 funcionalidades distintas.

A bateria da versão 1.0.1 passou com GCC 13.2 e MSVC 19.51: **104 verificações originais do núcleo, 1.318 de CPU ampliada, 12.325 de persistência, 17 de integração Win32 e 29 de regressão da interface**, totalizando **13.793 verificações**. As 1.261 situações da bateria de CPU incluem 7.880 passos comparados com um oráculo independente. As mutações de JSON são entradas inválidas geradas com semente fixa, distintas de teste de mutação do código-fonte.

O tempo do lote de 1.000 instruções continua registrado como benchmark, com meta de 2 segundos no ambiente de ensaio. A medição é separada das asserções funcionais para que a carga de outra máquina não seja confundida com defeito lógico. O relatório acadêmico PDF/DOCX conserva o registro original da etapa 1, com 104 + 18 verificações; o relatório de correções e seus logs documentam esta atualização.

Também é possível usar CMake diretamente:

```bat
cmake -S . -B build\cmake
cmake --build build\cmake --config Release
ctest --test-dir build\cmake -C Release --output-on-failure
```

Nesse caso, os executáveis ficam em `build\cmake\bin`. Em outros sistemas operacionais, o CMake compila apenas os testes do núcleo; a interface requer Windows.

Para evoluir o projeto, mantenha as regras da CPU no núcleo e a apresentação na interface. Ao alterar a semântica de uma instrução, atualize os testes e os exemplos correspondentes. Ao mudar o formato dos projetos, trate a versão do esquema e a compatibilidade de leitura.
