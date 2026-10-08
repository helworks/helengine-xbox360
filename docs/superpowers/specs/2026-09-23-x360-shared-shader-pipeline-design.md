# Xbox 360 no pipeline compartilhado de shaders

Data: 2026-09-23. Estado: proposta para revisão; nenhuma implementação autorizada por este documento.

## Objetivo

Usar os mesmos fontes, identidades de assets, variantes e contratos de compilação da engine para produzir shaders executáveis pelo backend Xbox 360. O usuário escreve o shader uma vez; o target produz microcódigo Xenos e metadados próprios. O trabalho paralelo do PS3 deve continuar sem disputas sobre os contratos compartilhados.

Não confundir integração do pipeline com suporte imediato a todos os recursos do Standard Shader. A primeira entrega comprova um subconjunto real de HLSL; as seguintes ampliam a cobertura explicitamente.

## Estado verificado

Raízes usadas abaixo: `X360 = C:/dev/helworks/helengine-x360`, `Engine = C:/dev/helworks/helengine`, `WiiU = C:/dev/helworks/helengine-wiiu`, `Vita = C:/dev/helworks/helengine-psvita`, `Xenia = C:/dev/helworks/reference/xenia-canary`.

| Área | Evidência atual | Consequência |
| --- | --- | --- |
| Contrato de compilação | `Engine/engine/helengine.shader/shaders/compilation/IShaderBackend.cs`, `ShaderCompileService.cs` | Implementar backend de target, sem criar outra API de autoria. |
| Separação host/runtime | `Engine/engine/helengine.shader.compilation/helengine.shader.compilation.csproj` compila arquivos linkados da pasta de compilation | Dependências de compilador ficam no builder, não no core gerado para o console. |
| Targets | `ShaderCompileTarget.cs` e `ShaderTargetDescriptors.cs`, em `engine/helengine.shader/shaders/runtime/` | Adicionar `Xbox360` / `x360` / `HEL_API_X360`, preservando os valores existentes e coordenando com PS3. |
| Cooking | `Engine/engine/helengine.baseplatform/Builders/IPlatformShaderArtifactBuilder.cs` e `engine/helengine.editor/managers/project/EditorPlatformAssetCookService.cs` | Já existem fontes resolvidos, dependências por par de programas e declarações de arquivos. Reutilizar. |
| Wii U | `WiiU/builder/shaders/WiiUGlslShaderBackend.cs` | HLSL passa por shaderc/SPIR-V e SPIRV-Cross para GLSL; não é um emissor Xenos reutilizável pronto. |
| Vita | `Vita/builder/shaders/PsVitaShaderBackend.cs` | Mesmo contrato aceita artefato nativo e compilador específico; há lowering especializado de ForwardStandard, não prova de compilação HLSL genérica. |
| Xbox atual | `X360/tools/xenos_asm.py`, `tools/build-shaders.ps1`, `src/platform/xenia_guest/shader.c` | Assembly limitado gera arrays compilados no executável. Seleção por `HelShaderKind`, não por asset. |
| Materiais Xbox | `builder/Xbox360MaterialCooker.cs`, `src/platform/xenia_guest/X360RenderManager3D.cpp/.hpp` | Cooker retorna payload antigo sem dependências de shaders; runtime guarda baseColor. A capacidade atual de UI/textura não equivale a materiais 3D texturizados completos. |
| Upload Xbox | `src/platform/xenia_guest/mesh.c`, `shader.c` | Constants, fetches e contagem de registradores estão codificados à mão. Precisam de um ABI explícito. |
| Xenia | `Xenia/src/xenia/gpu/shader_translator.cc`, `ShaderTranslator::TranslateAnalyzedShader` | Lê microcódigo Xenos e traduz para o host. Não resolve a direção HLSL/SPIR-V para Xenos. |

O grafo foi consultado pelo codebase-memory-mcp após a reconexão. A checagem de cobertura informou `metadata_changed` nos caminhos consultados; as conclusões acima foram confrontadas com trechos de fonte. Há parse parcial em HLSL e `ucode.h`: detalhes de instruções ainda exigem leitura direta durante a prova do compilador. A busca limitada nos documentos indexados do PS3 não estabeleceu um contrato novo de shaders; isso não significa ausência de trabalho em andamento.

## Decisão proposta: um backend Xenos, com prova de viabilidade primeiro

Fluxo proposto:

```text
HLSL + includes + defines + par/variante selecionados
  -> ShaderCompileRequest / backend Xbox360 no host
  -> shaderc -> SPIR-V normalizado e validado
  -> lowering Xenos + alocação de registradores + scheduling
  -> assembler Xenos + metadados físicos
  -> artefato versionado declarado pelo cooker
  -> conteúdo da imagem -> runtime material/programa -> PM4
```

SPIR-V é a proposta de entrada para o lowering, não uma capacidade já implementada. A primeira fase deve provar essa rota antes de comprometer a integração completa.

Alternativas consideradas:

- Compilador externo Xbox 360: manter como alternativa se houver ferramenta legitimamente disponível, reproduzível e adequada ao projeto. Não assumir XDK instalado nem redistribuir ferramentas proprietárias.
- Conversão própria de HLSL textual diretamente para `.xasm`: evitar como arquitetura principal; exigiria reinventar parsing, tipos e semântica. Não substituir expressões por regex.
- Mapear nomes como `ForwardStandardShader` para os blobs atuais: serve somente como baseline legado, não como compilação. Alterar o fonte precisa alterar o resultado; shaders desconhecidos não podem virar unlit silenciosamente.

Preservar o assembler existente como emissor inicial e oráculo de fixtures. Expandir somente as instruções exigidas pelo subconjunto comprovado. Não iniciar uma reescrita geral do pipeline Wii U/Vita/PS3 nesta entrega.

## Escopo incremental e critérios de suporte

1. **Prova do compilador:** VS/PS com posição transformada, cor, UV e amostragem 2D. Operações float/vetor/matriz, máscaras e swizzles suficientes para esses exemplos; recursos fora do subconjunto geram erro identificável.
2. **Pipeline integrado:** um material authored escolhe um par compilado pelo backend, o cooker declara os artefatos, e o runtime os carrega sem substituição por um enum fixo. Dois shaders de fonte diferente devem produzir comportamento diferente.
3. **Standard sem sombras:** avaliar o fonte compartilhado real, suas operações, buffers e texturas. Não renomear Lambert legado como Standard. Preservar semântica ou declarar um perfil restrito explicitamente acordado. Sombras, múltiplas luzes e demais recursos continuam rejeitados enquanto não implementados.
4. **Cobertura ampliada:** `ForwardStandardShadowed` e `ShadowDepth`, demais recursos e shaders customizados dentro de capacidades publicadas. Exige planejamento adicional do renderer, não apenas do compilador.

`StandardShaderVariants.All` define três variantes. Enquanto o target não cumprir o contrato exigido pelo consumidor, não anunciá-lo como Standard completo nem remover variantes globalmente para fazê-lo passar. Caso seja necessário suporte por perfil, acordar uma extensão de capabilities compartilhada com PS3; a classe atual informa modelo/stages/ray tracing, não uma matriz completa de recursos.

## ABI e formato de artefato

Proposta: envelope Xbox versionado, com identificação de target e estágio; identidade de asset/programa/entry point/variante; hashes do fonte resolvido e do payload; versão do compilador e ABI; microcódigo; entradas/saídas; recursos e layout físico.

- Serializar inteiros em ordem de bytes explícita, proposta little-endian no arquivo. O reader PowerPC decodifica para valores `uint32_t`; o upload usa a convenção já provada do ring. Não reinterpretar o arquivo diretamente como array nativo.
- Metadados de VS e PS devem distinguir contagem lógica de registradores dos campos codificados de `SQ_PROGRAM_CNTL`; hoje o código usa maior índice/count-minus-one. O packer de registradores deve ser testado contra o decoder.
- Refletir offsets/tipos/strides de membros, orientação de matrizes, mapa de cbuffers para bancos de constantes por estágio, entradas de vértice, interpoladores e pares textura/sampler para fetch slots.
- Não tratar os shifts lógicos de `ShaderBindingPolicies.Default` como índices físicos de fetch Xenos.
- O layout físico atual do vértice tem 12 dwords: posição, normal, UV e cor. Compatibilidade com o VS deve ser validada antes do draw.
- Não assumir que os registros atuais `c0..c12` comportam o Standard. Ele usa world, worldViewProj, normalMatrix, câmera, luzes, materiais e texturas adicionais.
- Validar comprimentos, aritmética de offsets, alinhamento, versões, limites de hardware/backend, par VS/PS e hashes antes de emitir comandos. Falhar sem upload parcial.
- Materiais referenciam artefatos por identidade estável e par/variante; o carregamento deduplica programas. A memória continua válida até o término de seu uso pelo GPU. Arquivo ausente/incompatível deve gerar erro, não material branco de sucesso.

Os campos binários exatos e os limites numéricos serão congelados depois da prova do compilador e da conferência da ISA. O limite atual de tamanho aceito em `validate_ucode` não é prova de que o shader cabe no orçamento de um frame.

## Cache, includes e diagnósticos

`ShaderCompileRequestIdentity` atualmente inclui fonte, programas, estágio, target, modelo, variante, defines e binding policy. Não inclui versão do compilador nem todos os flags de `ShaderCompileOptions`; dependências de include não aparecem como entradas independentes nesse método.

O target deve garantir identidade pelo fonte efetivamente resolvido, includes transitivos, opções, ABI e ferramenta. Um cache interno mais forte não corrige sozinho um hit incorreto no `ShaderCompileService`, que acontece antes de chamar o backend. Antes de habilitar cache compartilhado para x360, corrigir o contrato com o responsável pelos arquivos compartilhados ou usar cache desativado/isolado para esse serviço. Testar invalidation, não apenas repetição de hashes.

Erros devem informar asset, programa, estágio, variante e operação/recurso incompatível. Não publicar arquivos parciais como resultado válido. Falta de ferramenta e shader fora do subconjunto são diagnósticos diferentes.

## Preservação do renderer e coordenação PS3

- Manter os shaders legados de UI/texto e demos funcionando durante a migração. A migração deles para HLSL é posterior ao primeiro material end-to-end.
- Cache de upload passa de `HelShaderKind` para identidade do programa quando entrar o caminho novo; reset por frame continua explícito.
- Não aumentar o limite seguro do ring como atalho. Medir upload, trocas de programa, constantes e draw calls; rejeitar orçamento insuficiente. `PM4_IM_LOAD` é uma otimização futura, não pré-requisito desta proposta.
- Xbox possui builder, lowering, formato e runtime em `helengine-x360`. PS3 possui o correspondente em seu repositório.
- Compartilhados: targets/descriptors, identidade de cache, capabilities/perfis, fontes Standard e eventuais contratos baseplatform. Antes de editar, reler estado atual e combinar um responsável por cada mudança. Não escolher novos ordinais de enum independentemente em dois agentes.
- Não alterar arquivos PS3 nem refatorar os backends existentes para compartilhar um front-end antes de haver necessidade demonstrada.
- Sem worktrees, captura de tela, controle da área de trabalho ou abertura de emulador nesta fase de planos. Testes visuais futuros são observados pelo usuário; logs e resultados numéricos complementam a validação.

## Aceitação e decisões pendentes

Uma entrega integrada exige fonte authored -> artifact cook -> imagem -> shader realmente usado em múltiplos frames; mudança de fonte e de parâmetros deve aparecer no resultado. Cubo, sprites, texto e FPS permanecem funcionais; não basta apresentar um frame. Sucesso em Xenia não deve ser descrito como validação em console físico.

Decisões para a implementação: confirmar a rota open-source SPIR-V -> Xenos na prova; acordar ownership e IDs com PS3; escolher entre implementar a semântica Standard requerida ou propor perfil restrito explícito se os limites impedirem. Esses pontos são gates reais, não lacunas a esconder com shaders predefinidos.
