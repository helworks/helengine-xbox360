# Xbox 360 Shared Shader Pipeline — Implementation Plan

> Implementação autorizada pelo usuário em 2026-09-23, usando Luna. Trabalhar no checkout existente, sem worktrees. Revisão independente continua exigindo autorização separada. Progresso: [registro de implementação](x360-shader-implementation-progress.md).

**Goal:** conectar o target x360 ao pipeline compartilhado e executar shaders derivados do fonte authored, preservando o renderer atual.

**Architecture:** backend host `IShaderBackend`, proposta shaderc/SPIR-V -> lowering/assembler Xenos, cooker `IPlatformShaderArtifactBuilder`, artefatos versionados e runtime orientado a programas.

**Tech Stack:** C#/.NET 9 no builder; shaderc/SPIR-V no host; Python no assembler existente; C/C++ PowerPC no runtime; validação Xenia.

**Spec:** [Design](../specs/2026-09-23-x360-shared-shader-pipeline-design.md).

## Global Constraints

- Implementação incremental autorizada, respeitando os gates. Nomes de novos arquivos abaixo são propostos, não APIs já existentes.
- `Engine` significa `C:/dev/helworks/helengine`; demais caminhos sem prefixo pertencem a este repositório.
- Preservar alterações locais de `X360RenderManager2D.cpp`, `video.c`, `tools/test-host.ps1` e `.superpowers/`.
- Sem converter DX11 bytecode em microcódigo por simples empacotamento; sem trocar fonte authored por blob escolhido pelo nome.
- Recursos não implementados falham no cook; não são ignorados. Não anunciar Standard completo prematuramente.
- A sequência abaixo tem gates. Uma prova que falhe deve produzir diagnóstico e revisão do plano, não integração fictícia.

## Review Focus

Conversão de semântica/matrizes, alocação e scheduling, ABI de constants/fetches, ordem de bytes, isolamento de cache, contratos PS3 concorrentes, validação de arquivos e orçamento do ring.

## Fase 0 — Fixar baseline e fronteiras compartilhadas

**Files:** consultar `builder/Xbox360PlatformAssetBuilder.cs`, `Xbox360MaterialCooker.cs`, `src/platform/xenia_guest/{shader.c,mesh.c,video.c}`, e os caminhos compartilhados listados no design. Criar `docs/x360-shader-abi.md` somente durante implementação.

**Consumes:** fontes atuais e alterações paralelas PS3. **Produces:** ownership, matriz de capacidades inicial e baseline reproduzível.

- [ ] Consultar codebase-memory-mcp, checar cobertura dos caminhos e ler diretamente os trechos stale/parciais. Revalidar os contratos; o plano é um snapshot.
- [ ] Registrar `git status`, revisão e shaders atuais. Não reverter nem incorporar alterações alheias.
- [ ] Combinar com o responsável pelo PS3 quem altera enum/descriptors, cache e capabilities. Preservar valores já serializados; decidir os novos IDs uma única vez.
- [x] Executar `python tests/xenos_asm_test.py` e `dotnet build builder/helengine.x360.builder.csproj`. Baseline: 13 testes passaram; build sem restore passou sem avisos/erros. Suíte C bloqueada pela ausência de gcc no PATH.
- [ ] Inventariar por fonte os requisitos de `ForwardStandardShader.hlsl` e `StandardShaderVariants.All`. Classificar o que cabe na prova e o que exige trabalho posterior.

**Exit:** baseline conhecido e mudanças compartilhadas delimitadas. Não abrir Xenia para tarefas puramente documentais.

## Fase 1 — Provar HLSL -> Xenos sem mexer no renderer de produção

**Existing:** `tools/xenos_asm.py`, `tests/xenos_asm_test.py`, `shaders/*.xasm`.

**New, proposed:** `tools/shader-probe/` para o host probe; `tests/shaders/fixtures/` para HLSL, valores esperados e microcódigo de referência; `docs/evidence/x360-shader-compiler/` para resultados textuais.

**Consumes:** HLSL mínimo e ISA documentada. **Produces:** microcódigo de VS/PS e metadados suficientes para um draw.

- [ ] Fixar versão/configuração shaderc, preservar entry point/defines e gerar SPIR-V validado para os exemplos: cor uniforme, transformação não simétrica, cor interpolada e textura 2D.
- [ ] Inventariar opcodes efetivamente produzidos; especificar tipos, valores constantes, loads/stores, interfaces e operações aceitas. Rejeitar o restante com diagnóstico.
- [ ] Implementar o menor lowering real: representar valores, mapear operações, alocar temporários com análise de vida, inserir fetch/export, respeitar dependências e construir exec/alloc blocks. Não presumir que emitir uma instrução por operação resolve scheduling.
- [ ] Usar o assembler existente para emissão. Onde faltar encoding, conferir diretamente a ISA/decoder local do Xenia, adicionar fixture independente e só depois expandir o assembler.
- [ ] Verificar decodificação, saídas numéricas conhecidas e limites de registradores. Usar matrizes não simétricas para revelar transpose incorreto; amostras UV e alpha para revelar erros de interpolação/textura.
- [ ] Provar que duas expressões HLSL diferentes mudam o resultado; testar entrada malformada, recurso não suportado e pressão de registradores acima do limite.
- [ ] Medir tamanho de microcódigo e custo do upload. Documentar os comandos exatos do probe após existir, com versões das ferramentas.

**Gate A:** só continuar se a rota produzir e executar VS/PS corretos. Se falhar, registrar a limitação e comparar compilador externo legitimamente disponível versus ampliar o emissor. Não transformar o spike em compromisso de implementar toda a ISA.

## Fase 2 — Registrar o target e consolidar o backend host

**Modify:** `builder/helengine.x360.builder.csproj`, `builder/Xbox360PlatformAssetBuilder.cs`.

**Shared, coordinated:** `Engine/engine/helengine.shader/shaders/runtime/{ShaderCompileTarget.cs,ShaderTargetDescriptors.cs}`; se necessário, `shaders/compilation/{ShaderCompileRequestIdentity.cs,ShaderCompileCacheKey.cs,ShaderCompileService.cs}` no mesmo projeto de fontes.

**New, proposed:** `builder/shaders/Xbox360ShaderBackend.cs`, `Xbox360ShaderCompiler.cs`, `Xbox360ShaderCompileIdentity.cs`; `builder.tests/helengine.x360.builder.tests.csproj` e testes de backend/identidade.

**Consumes:** `ShaderCompileRequest`, `IShaderIncludeResolver`. **Produces:** `ShaderCompileResult` com microcódigo e metadados corretos.

- [x] Referenciar o projeto host `helengine.shader.compilation`, sem levar shaderc ou execução de processos para o core gerado.
- [x] Registrar `Xbox360 = 8` / `x360` / `HEL_API_X360`, preservando `Ps3 = 7` e os demais IDs; testes de descritores aprovados.
- [x] Contribuir o backend real via `IShaderBackendRegistryContributor` no builder.
- [ ] Promover o compilador aprovado no Gate A a componente único reutilizado por editor/cooker; não manter duas implementações concorrentes do lowering.
- [x] Preservar fonte, defines, variantes, entry points, includes e diagnósticos pelo contrato existente. Publicar somente stages/modelo realmente aceitos.
- [x] Resolver cache antes de habilitá-lo: incluir opções, versão do compilador/ABI e includes efetivos no nível que precede o lookup compartilhado, ou desabilitar esse cache para a primeira entrega.
- [ ] Testar recompilação após mudar include, opção, variante, target ou versão de ferramenta; garantir que falha não reaproveita binário antigo.

**Verify:** `dotnet test builder.tests/helengine.x360.builder.tests.csproj`; suíte compartilhada afetada pelo patch coordenado. Não alterar snapshots globais só para esconder incompatibilidade.

## Fase 3 — Congelar o ABI e o artefato

**New, proposed:** `builder/shaders/Xbox360ShaderArtifact.cs`, `Xbox360ShaderArtifactSerializer.cs`; `src/platform/xenia_guest/x360_shader_artifact.h`, `x360_shader_artifact.c`; `tests/shader_artifact_test.c`; finalizar `docs/x360-shader-abi.md`.

**Consumes:** microcódigo e reflexão física do compilador. **Produces:** formato versionado escrito no host e lido no guest.

- [ ] Especificar magic/version, endianness, limites, offsets, checksums, identidades e layout de recursos. Não serializar structs C# ou C por memória bruta.
- [ ] Fixar mapa lógico -> físico por estágio, matrizes, constantes imediatas, entradas/varyings, slots textura/sampler e codificação de program control.
- [ ] Criar fixture binária conhecida consumida por C# e C. Testar campos acima de 255 para detectar troca de bytes acidental.
- [ ] Rejeitar truncamento, overflow de offsets/tamanhos, target/stage incorretos, shader grande demais, slot fora do limite, par incompatível e hash inválido.
- [ ] Separar parser puro e emissão PM4; não haver escrita de comandos no caminho de validação.

**Verify:** testes .NET e teste C compilado com compilador host disponível. Documentar o comando C no projeto de testes; não declarar sucesso se gcc/clang estiver ausente.

## Fase 4 — Conectar cooking de shaders e materiais

**Modify:** `builder/Xbox360PlatformAssetBuilder.cs`, `Xbox360MaterialCooker.cs`, `Xbox360PlatformDefinitionFactory.cs`, `Xbox360ContentStager.cs`; ajustar `Xbox360SceneCompatibilityValidator.cs` somente conforme as capacidades comprovadas.

**New, proposed:** `builder/Xbox360ShaderArtifactCooker.cs`, `Xbox360CompiledShaderMaterialSerializer.cs`, testes de cook/staging.

**Consumes:** `PlatformMaterialCookRequest`, `PlatformShaderArtifactCookRequest`, fontes resolvidos e `PlatformShaderDependency`. **Produces:** payload de material e declarações explícitas dos artefatos.

- [ ] Implementar `IPlatformShaderArtifactBuilder`. Exigir fonte correspondente e par selecionado; não hardcodear todos os entry points para `VS`/`PS` em shaders arbitrários.
- [ ] Fazer o material retornar dependências reais e carregar identidade de par/variante. Preservar payload legado por versão/rota explícita, sem detecção por tentativa.
- [ ] Cozinhar apenas dependências suportadas, com caminhos livres de colisão entre assets/programas/variantes. Compartilhar artefatos idênticos de forma determinística.
- [ ] Publicar arquivos completos atomicamente e declarar todos no manifest; impedir path traversal. Erro no cook não deixa um artefato anterior parecer atual.
- [ ] Validar textura, blend, layout de vértice e esquema de material contra a capacidade do runtime, não apenas contra aceitação do compilador.
- [ ] Testar cook completo de dois materiais, deduplicação, fonte ausente, variante incompatível e staging da imagem contendo os artefatos declarados.

**Exit:** não precisar modificar o empacotador central se os contratos atuais forem suficientes; qualquer extensão é uma mudança compartilhada separada, coordenada com PS3.

## Fase 5 — Carregar programas no runtime e executar o caminho integrado

**Modify:** `src/platform/xenia_guest/{shader.c,mesh.c,xenia_guest.h,X360RenderManager3D.cpp,X360RenderManager3D.hpp}`; integrar novos fontes no build existente em `tools/build.ps1`/makefiles conforme descoberta atual.

**New, proposed:** `src/platform/xenia_guest/X360CompiledShaderMaterialReader.cpp/.hpp` e armazenamento de programas no módulo de shaders.

**Consumes:** material cozido, reader de artefato e conteúdo. **Produces:** programa validado usado pelo draw com recursos corretos.

- [ ] Adicionar handles de programa/par com ownership e descarte seguros; deduplicar por identidade. Manter os shaders legados para UI/texto e demos.
- [ ] Carregar o par referenciado pelo material através do content source. Registrar erro útil para arquivo inválido/ausente, sem retornar sucesso branco.
- [ ] Bindar dados segundo metadados, incluindo bancos distintos de VS/PS. Validar layout de vértice e compatibilidade de varyings.
- [ ] Alterar seleção do caminho novo para programa do material, não presença de textura. O shader texturizado atual é de UI e não pode substituir automaticamente um shader 3D.
- [ ] Cachear upload por identidade e invalidar corretamente por frame/troca de caminho. Validar capacidade do ring antes de emissão; não elevar o limite seguro para acomodar uploads.
- [ ] Cobrir alternância A/B/A de materiais, muitos draws compartilhando programa, troca entre cena e texto e liberação/recarregamento.

**Gate B:** fonte -> cook -> conteúdo -> material -> draw funciona, e modificar a expressão do shader muda o resultado sem editar código C/assembly.

## Fase 6 — Standard Shader e expansão de cobertura

**Files:** backend/ABI/runtime das fases anteriores; `Engine/engine/helengine.editor/shaders/builtin/ForwardStandardShader.hlsl` e `engine/helengine.shader/shaders/StandardShaderVariants.cs` são referências compartilhadas, não arquivos de livre alteração pelo agente Xbox.

- [ ] Comparar SPIR-V do Standard real com o subconjunto aprovado: normalização, divisão/potências, branches, quantidade de recursos e layout de luzes/materiais. Produzir lista concreta de lacunas.
- [ ] Implementar operações e binding requeridos para a semântica escolhida, começando sem sombras. Verificar normalMatrix com escala não uniforme e propriedades do material com valores distintos.
- [ ] Se necessário um perfil restrito, apresentá-lo explicitamente e coordenar capabilities com PS3. Não usar redução Lambert silenciosa nem remover variantes compartilhadas.
- [ ] Planejar separadamente sombras/passes/render targets antes de declarar `ForwardStandardShadowed`/`ShadowDepth`. Até lá, rejeitar seleção incompatível com diagnóstico.
- [ ] Depois da estabilidade 3D, migrar shaders legados de UI/texto para o mesmo fluxo, preservando atlas, alpha e batching; não misturar essa migração com o primeiro bring-up.

**Gate C:** declarar precisamente o perfil e recursos suportados; integração do pipeline não equivale a paridade visual total com DX11/Vulkan.

## Fase 7 — Validação final e entrega

- [ ] `python tests/xenos_asm_test.py`.
- [ ] `dotnet test builder.tests/helengine.x360.builder.tests.csproj` após a criação do projeto.
- [ ] `dotnet build builder/helengine.x360.builder.csproj` e testes compartilhados efetivamente afetados.
- [ ] Build da imagem por `tools/build.ps1` com core/conteúdo regenerados pelo pipeline novo. Reusar somente bibliotecas compatíveis; os artefatos antigos em `build/` não provam integração.
- [ ] Exercitar em Xenia a cena com dois shaders authored, transformações, textura, parâmetros de material e UI/texto. Usar lançamento CLI quando autorizado, sem CUA, captura ou ativação forçada de janela.
- [ ] Registrar hashes dos fontes/artefatos/XEX, programas carregados, draws, dwords por frame e progresso de pelo menos 600 frames sem erro de ring/fence. Usuário confirma o resultado visual, incluindo texto e linha de FPS.
- [ ] Testar falhas controladas de shader incompatível, artefato ausente e cache obsoleto; não esconder regressões usando fallback legado.
- [ ] Entregar documentação de autoria, limites conhecidos, comandos reproduzíveis e estado de validação (Xenia versus hardware). Commit somente quando solicitado.

## Estado deste plano

Fase 3a: snapshots imutáveis, validação de metadados e vínculo VS/PS implementados.
A verificação local do coordenador passou 80/80 testes (48 novos). A revisão
delimitada terminou sem bloqueios funcionais; limitações estão registradas no
relatório da tarefa. Formato binário, leitor C, cooker e
integração com o renderer continuam pendentes; este resultado não conclui a Fase 3.

Atualização de 2026-09-24: o descriptor do Xbox original já está presente; aquela
pendência compartilhada foi resolvida. A última repetição completa passou 32/32
testes .NET. Antes dela houve uma falha isolada no teste de timeout do processo,
que passou na repetição focada e na suíte completa; a intermitência permanece
registrada, sem enfraquecer o teste. A Fase 3a está em implementação: snapshots
imutáveis dos programas e validação do par VS/PS, antes do formato binário e
do leitor C. O histórico abaixo registra o estado anterior da Fase 2.

Implementação iniciada com Luna. A prova CPU em `tools/shader-probe/` já converte HLSL/SPIR-V para microcódigo Xenos no subconjunto de transformação/cor, com execução numérica do binário em testes e CLI nas duas rotas. O registro de implementação e os relatórios vinculados documentam a validação e os ajustes de revisão.

O lowering agora também aceita o subconjunto pixel Texture2D<float4> + SamplerState, com UV normalizado, amostragem implicit-LOD simples e mapeamento explícito de pares para fetch slots. A prova CPU usa um sampler point/clamp/no-mip, não valida derivadas nem filtragem real da GPU. Ver [relatório de texturas](x360-shader-texture-report.md).

Gate A validado em 2026-09-23 para o par restrito de transformação/cor/textura 2D: o usuário confirmou o quad junto da cena/texto no Xenia, e o log registra mais de 600 apresentações. Primeiro frame: 7.784 dwords, abaixo do limite seguro de 8.192. Evidência e limites em [shader-gpu-probe](../../shader-gpu-probe.md). Não há validação em hardware nem comparação visual pixel a pixel.

Fase 2 agora tem constantes simbólicas, layout/reflexão derivados do SPIR-V, protocolo host reutilizável e backend C# registrado sem ativar a seleção legada de materiais. O serviço compartilhado recompila pelo backend Xbox 360 sem usar seu cache; includes efetivos, opções, entry points, variantes e identidades chegam ao compilador único. PS3 e IDs existentes foram preservados. Protocolo e backend foram revisados, incluindo as correções de reflexão e timeout. Os 91 testes Python passaram; o builder compila. Na repetição local .NET, 31 de 32 testes passaram, incluindo todos os 24 novos testes do backend: uma adição concorrente de `Xbox = 9` para o Xbox original ainda não tem descriptor e quebra a verificação de completude compartilhada. Essa pendência exige coordenação e nova execução da suíte, não enfraquecimento do teste. Evidências e estado atualizado ficam no [registro de implementação](x360-shader-implementation-progress.md). Formato final, cooking de materiais, runtime integrado e Standard Shader permanecem pendentes.
