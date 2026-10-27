# ChipLab 1.0.1 - bateria de testes e correções

Data de referência: 26/10/2026. Atualização da aplicação da primeira etapa, em C++17/Win32.

## Resultado

Cinco suítes passaram com **GCC 13.2 e MSVC 19.51**, em Windows de 64 bits. A execução completa registrou **13.793 verificações**. O executável entregue foi gerado pelo GCC, com runtime estático. O teste de importações identificou somente bibliotecas Windows/UCRT.

| Suíte | Verificações aprovadas | Escopo |
| --- | ---: | --- |
| Núcleo original | 104 | ISA, execução, limites e projetos |
| CPU ampliada | 1.318 | 1.261 cenários determinísticos; 7.880 passos comparados com oráculo independente |
| Persistência ampliada | 12.325 | JSON, Unicode, limites, arquivos e concorrência |
| Integração Win32 original | 17 | Controles reais, execução, CSV e reinicialização |
| Regressões da interface | 29 | Colagem, Unicode, parada, limite, arquivos e mensagens |
| **Total** | **13.793** | Asserções e verificações; não equivale a esse número de funcionalidades distintas |

As 27 combinações de largura de palavra, banco de registradores e memória foram exercitadas. O teste de persistência processou 12.000 entradas JSON mutadas com semente fixa e 100 ciclos de salvar/abrir rascunhos. Isso é fuzzing de entradas; não constitui teste de mutação do código-fonte.

## Falhas reproduzidas e corrigidas

| ID | Reprodução e comportamento anterior | Correção e regressão |
| --- | --- | --- |
| BUG-001 | Colar uma fonte válida de 100000 bytes LF, expandida para CRLF no controle Win32, deixava somente 75000 bytes lógicos. A API de definir texto usada no teste anterior contornava o limite do controle e escondia a falha de colagem. | Espaço suficiente em UTF-16 no controle; validação de 100000 bytes no núcleo. Regressão usa EM_REPLACESEL, preserva a fonte integral e monta HALT. |
| BUG-002 | Um nome válido com 60 caracteres fora do plano Unicode básico era cortado para 30, porque o controle contava 60 unidades UTF-16. | O controle acomoda os pares substitutos; a validação conta pontos Unicode. O teste aceita 60 e rejeita 61 sem convertê-los silenciosamente em 60. |
| BUG-003 | Digitar/colar 10000 no campo de limite produzia 1000 por truncamento. | A entrada completa chega à validação e é rejeitada sem alterar a CPU. |
| BUG-004 | Após stop(), o painel podia continuar indicando EXECUTANDO, inclusive no caminho de tratamento de erro. | Parar sincroniza os controles e o painel. O teste confere estado e botões. |
| BUG-005 | A conversão UTF-16 para UTF-8 substituía pares Unicode inválidos por um caractere de substituição. Uma mensagem de exceção com bytes fora de UTF-8 também podia lançar outra exceção no tratador. | Conversão de dados estrita; diagnóstico com mensagem de recuperação segura. Testes cobrem substitutos isolados e mensagem de exceção inválida. |
| BUG-006 | Assembly ou JSON com BOM UTF-8 inicial eram rejeitados. | Reconhecimento de um único marcador inicial, preservando limites e sem aceitar marcadores extras na sintaxe. O rastro mostra a instrução sem o marcador. |
| BUG-007 | Nomes com controles ou somente espaços Unicode eram aceitos. | Validação de controles C0/C1 e espaços Unicode; testes de ida e volta preservam nomes válidos. |
| BUG-008 | A compilação MSVC falhava porque small, usado como nome de um campo de fonte, é uma macro do Windows SDK. | Campo renomeado para smallFont. Aplicação e testes compilam nos dois compiladores; a última recompilação da regressão está sem avisos. |

## Proteções adicionais

- O passo da CPU agora rejeita configuração, dimensões, operandos e saltos internos inconsistentes antes de alterar PC, ciclos, registradores, memória ou rastro. Esses cenários são defesa da API C++ e não são produzidos pela interface normal. Antes da correção, um destino adulterado 999 podia ser confirmado no PC; um imediato adulterado 999 podia virar 231 em 8 bits.
- CSV e Verilog usam o mesmo mecanismo de gravação segura dos projetos: temporário exclusivo, sincronização, fechamento e substituição. Ensaios com destino bloqueado verificam preservação do arquivo anterior e limpeza dos temporários.
- A leitura de arquivo na interface é limitada a 1 MiB mais um byte de detecção, sem depender de uma consulta prévia de tamanho que pudesse ficar desatualizada.
- Carregamentos validam configuração, fonte e conversão antes de substituir os controles.
- Os testes geram arquivos em pastas de teste explícitas ou temporárias exclusivas. Eles não sobrescrevem por padrão os exemplos e as evidências de entrega.

## Validação de compilação e desempenho

| Execução | Resultado |
| --- | --- |
| CMake Release / GCC 13.2 | 5/5 suítes aprovadas |
| CMake Release / MSVC 19.51 | 5/5 suítes aprovadas |
| TESTAR.cmd com diretório contendo espaços | Aprovado, código de saída 0 |
| Recompilação final da regressão Win32 nos dois compiladores | Aprovada, sem avisos |
| Lote de 1000 saltos, GCC | 1433,79 ms no ensaio CTest |
| Lote de 1000 saltos, MSVC | 1428,09 ms no ensaio CTest |

O benchmark usa temporizador Win32 real, janela oculta e arquitetura 8/8/64. Os dois ensaios atenderam à meta local de até 2000 ms; outras execuções também estão nos logs. O tempo é registrado separadamente das asserções funcionais, pois varia com o ambiente e sua carga.

O MSVC instalado apresentou uma falha de seu gerenciador de PDB durante a configuração inicial do CMake. A validação usou informações de depuração incorporadas (Embedded) no ambiente de teste. Esse contorno não altera o código do produto. O teste também identificou e corrigiu um aviso em um byte inválido criado de propósito pela própria regressão; sua recompilação final está registrada.

## Reprodução

Com um compilador C++17 instalado, executar TESTAR.cmd na pasta do projeto. Para isolar a compilação, passar uma pasta como primeiro argumento. O script retorna erro se qualquer compilação ou suíte falhar.

Os fontes de teste e o CMake estão no pacote. As evidências desta execução ficam em `evidencias/correcoes_1.0.1`: resumos CTest, saída integral do script, compilações e reproduções antes/depois. Os logs anteriores da etapa 1 são históricos e permanecem identificados separadamente.

## Limites

Os testes não demonstram ausência de todo defeito. Não foram medidos cobertura estrutural nem escore de mutação. ASan/UBSan não estavam disponíveis no GCC instalado; a bateria da CPU também foi executada com verificações da biblioteca padrão habilitadas. Não houve síntese ou simulação HDL externa. Compatibilidade de interface foi verificada neste Windows, não em todos os computadores possíveis.
