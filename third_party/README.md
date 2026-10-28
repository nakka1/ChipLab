# Componentes incluídos

O aplicativo usa as APIs C nativas destes projetos. Os cabeçalhos em `include` e os arquivos de `runtime` correspondem às versões abaixo. O pacote funciona sem instalar Python; os wheels foram usados apenas como meio de obtenção dos binários oficiais para Windows x64.

| Componente | Versão | Uso | Licença / material incluído |
| --- | --- | --- | --- |
| [NASM](https://www.nasm.us/) | 3.02 | Montagem Intel/NASM para binário de 32 bits | BSD-2-Clause; `nasm/LICENSE` |
| [Unicorn](https://www.unicorn-engine.org/) | 2.1.4 | Execução em CPU e memória emuladas | GPL-2.0; `unicorn/notices/src/qemu/COPYING`; fonte completo `unicorn/unicorn-2.1.4.tar.gz`, com os avisos de seus componentes |
| [Capstone](https://www.capstone-engine.org/) | 5.0.9 | Decodificação dos bytes em instruções Intel | BSD-3-Clause; `capstone/LICENSE.TXT` |
| Microsoft Visual C++ Runtime | 14.51.36247.0 | Dependência nativa de `unicorn.dll` | Microsoft Corporation; `runtime/vcruntime140.dll`, componente redistribuível do Visual Studio |

O `vcruntime140.dll` foi obtido da pasta oficial `VC/Redist/MSVC/14.51.36231/x64/Microsoft.VC145.CRT` da instalação local do Visual Studio. Ele conserva a assinatura e os metadados originais da Microsoft. Seus termos próprios não são substituídos pela licença dos outros componentes.

`dependencies.json` registra os URLs exatos dos arquivos de origem e os SHA-256 verificados. O campo `runtime_files` contém os hashes dos arquivos efetivamente incluídos. Não há download durante o uso do ChipLab.

O código-fonte do ChipLab desta distribuição é disponibilizado sob GPL-2.0-or-later; consulte `../COPYING`. As dependências mantêm seus próprios avisos e condições. O código-fonte do aplicativo, os scripts de compilação e o fonte correspondente do Unicorn acompanham esta distribuição.

Referências de integração: [API Unicorn](https://www.unicorn-engine.org/docs/tutorial.html), [API Capstone](https://www.capstone-engine.org/lang_c.html) e [manual NASM](https://www.nasm.us/docs.html).
