# Apresentação do ChipLab em 5 a 7 minutos

Unijorge · Engenharia da Computação · Grupo 2 · Salvador · Etapa 1 · 27/10/2026

Integrantes: Alexandre, Eliel, Leonardo, Luan, Maitê, Marcos, Whanderlon e Willian.

## Preparação

1. Execute o projeto seguindo o README no computador de apresentação.
2. Abra ChipLab.exe no Windows 64 bits, abra o programa de soma de 1 a 5 e deixe a arquitetura 8 bits, 8 registradores e 64 palavras.
3. Deixe disponíveis o relatório, a SRS, a matriz CSV, os registros de teste e uma pasta para exportação.
4. Complete os sobrenomes no material institucional, se solicitados. A distribuição de falas abaixo é sugerida, não um registro de atividade realizada.

## Falas sugeridas

- 0:00 a 0:40 — Alexandre: apresente o problema. “O ChipLab é um sistema desktop para explorar registradores, memória e instruções de uma arquitetura educacional própria.” Delimite que a ALU é o único bloco exportado em Verilog.
- 0:40 a 1:20 — Eliel: mostre F1, pedido real para ver código e memória simultaneamente. Cite a alternativa F2, conversor Verilog para Logisim, e explique o risco maior de parser e elaboração de circuitos. Distinga necessidade da fonte e decisões da equipe.
- 1:20 a 2:00 — Leonardo: explique configuração, montagem e núcleo separado da interface. Destaque ISA própria, memória em palavras e aritmética módulo 2^bits.
- 2:00 a 3:10 — Luan: demonstre montagem, Passo e Executar; mostre código e memória na mesma janela. Ao fim de HALT, destaque R0 = 0, R1 = R2 = memória[0] = 15 e 20 ciclos.
- 3:10 a 3:50 — Marcos: mostre uma entrada inválida e a linha do erro. Depois demonstre o laço JMP 0 com lote pequeno. Explique por que esses ramos e limites permitem testes funcionais.
- 3:50 a 4:30 — Willian: salve e reabra um projeto JSON, exporte o rastro CSV e a ALU Verilog. Explique que o JSON preserva fonte e arquitetura e que a CPU aguarda montagem ao abrir.
- 4:30 a 5:10 — Whanderlon: apresente o comando de testes do README e o registro real obtido. Não anuncie cobertura, mutação ou auditoria externa se essas métricas não foram medidas.
- 5:10 a 6:10 — Maitê: mostre um requisito e o caminho na matriz até componente e CT. Apresente a transferência ao Grupo 1 e à professora, a entrega de auditoria em 24/11 e a manutenção para avaliação até 16/12.

## Demonstração com resultado esperado

1. Monte o programa de soma fornecido. Esperado: nenhuma falha de montagem.
2. Execute um passo. Esperado: um ciclo adicional, alteração correspondente à primeira instrução e uma linha no rastro.
3. Reinicie e execute com limite 1000. Esperado: HALT, R0 = 0, R1 = R2 = memória[0] = 15, PC = 8 e 20 ciclos.
4. Exporte CSV. Esperado: cabeçalho e uma linha por instrução realmente executada.
5. Salve o projeto, reinicie e reabra o arquivo. Esperado: fonte e parâmetros preservados; CPU aguarda montagem. Acione Montar para iniciar o estado zerado.
6. Gere Verilog. Esperado: módulo da ALU com largura 8 e cinco operações. Não afirmar que é uma CPU completa.
7. Para o erro controlado, faça uma cópia da fonte e use R99 como registrador. Esperado: rejeição com linha informada. Recarregue o exemplo válido.
8. Monte JMP 0 e execute limite 10. Esperado: retorno após 10 instruções, sem esperar que o programa termine sozinho.

## Transferência formal

Entregue o pacote com código fonte, README, exemplos, testes, evidências e documentação ao Grupo 1 e à professora durante a apresentação. Registre data, versão e confirmação de recebimento. Esta instrução é para a equipe; o pacote não envia arquivos por e-mail nem realiza a submissão automaticamente.

Os critérios acima são resultados esperados para o ensaio. O registro efetivo fica em evidencias. Auditoria cruzada, análise de cobertura, grafo de fluxo e escore de mutação pertencem à etapa 2 e devem ser produzidos com dados reais.
