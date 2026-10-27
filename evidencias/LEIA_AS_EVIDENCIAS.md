# Verificação interna da etapa 1

Data de referência: 26/10/2026. Projeto: ChipLab, C++17, Unijorge, Grupo 2.

O núcleo passou por **9 grupos e 104 verificações**. A integração passou por **18 verificações**, usando a própria classe da aplicação e controles Win32 reais em uma janela oculta. Os registros completos estão em `testes_automatizados.txt` e `testes_integracao_win32.txt`. Para reproduzir, execute `TESTAR.cmd` na pasta do projeto; o compilador C++ é necessário apenas para recompilar os testes.

O ensaio de integração inclui execução por passo, soma completa, rastro CSV, limites 0/10/1000/1001, retomada de laço, erro de montagem sem substituição da CPU válida, configuração de 32 bits com o último endereço de memória, reinicialização e fonte de 100.000 bytes após conversão de quebras de linha. O teste de salvamento do núcleo verifica a preservação do arquivo anterior quando a substituição é impedida por um bloqueio real no Windows.

## Ambiente e medição

- Sistema informado pelo ambiente: Microsoft Windows NT 10.0.26200.0, 64 bits.
- Identificação do processador: AMD64 Family 25 Model 33 Stepping 2, AuthenticAMD.
- Compilador: GCC 13.2.0, MinGW-w64/UCRT64; C++17 e bibliotecas de runtime vinculadas estaticamente.
- Lote medido: `JMP 0`, 1.000 instruções, arquitetura 8 bits, 8 registradores, 64 palavras, temporizador Win32 real.
- Resultado registrado no log de integração: **1.339,74 ms**, em uma execução em janela oculta. A meta local de até 2 segundos foi satisfeita nesse ensaio. Uma repetição por script também passou; os tempos podem variar conforme a carga e o computador.

A inspeção de importações do executável identificou somente bibliotecas Windows/UCRT, sem DLLs adicionais do MinGW. O teste de funcionamento foi realizado no ambiente acima; outras versões do Windows ainda precisam de verificação própria.

## Inspeção da interface visível

1. O exemplo de soma terminou em HALT com PC 8, 20 ciclos, R0 = 0, R1 = R2 = memória[0] = 15. A captura `interface_soma_15.png` mostra a versão de entrega.
2. O laço `JMP 0`, com limite 10, retornou com PC 0 e 10 ciclos, sem bloquear a aplicação. A captura `interface_limite_10.png` registra esse resultado.
3. A tentativa de montar `MOV R99, 1` foi rejeitada com indicação da linha 1 e dos registradores permitidos. O programa anterior, o rastro e os 10 ciclos permaneceram disponíveis. As capturas `validacao_R99_0.png` e `validacao_R99_1.png` mostram a janela e sua mensagem.

As duas últimas capturas são da revisão anterior às correções de normalização e salvamento. O mesmo comportamento foi coberto novamente pelos testes de integração da versão final.

## Arquivos de exemplo gerados pela implementação

- `soma.chiplab.json`: configuração e fonte de demonstração, sem instantâneo da CPU.
- `soma_rastro.csv`: cabeçalho e 20 registros de instruções, com alterações e HALT.
- `chiplab_alu.v`: ULA combinacional de 8 bits, com cinco operações; não contém uma CPU completa.
- `limite_fonte.chiplab.json`: projeto contendo fonte com exatamente 100.000 bytes antes do escape JSON. O arquivo JSON é maior devido aos escapes das quebras de linha.

A exportação Verilog foi inspecionada e verificada textualmente. Não foi executada síntese, análise de temporização nem simulação HDL externa.

Estes registros são verificações internas de desenvolvimento. Os 24 casos de aceitação da matriz mantêm sua identificação para a auditoria independente. Cobertura estrutural, mutação e auditoria cruzada pertencem à etapa 2; não há métricas desses processos nesta entrega.
