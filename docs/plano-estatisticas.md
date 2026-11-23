# Plano: estatísticas de leitura, gamificação e tela de descanso

Quatro entregas, em ordem de dependência:

1. um agregador que transforma o log de sessões em números baratos de ler;
2. uma definição confiável de "livro terminado";
3. uma aba própria de estatísticas, com os livros em capa;
4. gamificação e um modo de tela de descanso que mostra o resumo.

Tudo abaixo foi lido deste repositório na branch `develop`.

---

## Aviso de escopo, antes de qualquer linha

`SCOPE.md:16` diz, com todas as letras, que estatística é coisa que o upstream
**deixa para os forks**:

> *Is another popular CrossPoint fork already solving this well? If yes, we
> generally defer to that fork if it's not part of the core reading experience.
> e.g. stats*

Então isto não é candidato a PR upstream. Trate como código seu e aplique a
disciplina que mantém o merge limpo: **tudo novo em arquivos novos**
(`src/stats/`, `host-tests/` ou `test/`, e esta pasta de docs), e o mínimo
possível de linhas em arquivos que o upstream possui. Os pontos onde encostar em
arquivo do upstream estão marcados com ⚠ abaixo — são sete, todos de poucas
linhas.

---

## O que já existe, e é a matéria-prima

| Fonte | O que dá |
| --- | --- |
| `/.crosspoint/reading_sessions.jsonl` | `doc`, `start` (Unix), `secs` (segundos **ativos**), `tz`, `from`/`to` (0..10000), `path`, `title` — histórico inteiro, nunca truncado (`src/sync/ReadingSessionLog.h`) |
| `.crosspoint/epub_<hash>/book.bin` | título, autor, **idioma**, e `cumulativeSize` do spine = tamanho do texto (`docs/file-formats.md`) |
| `.crosspoint/library.idx` | todos os livros do cartão, autor agrupado por `authorKey`, data FAT de chegada |
| `/Vocabulary.txt` | palavra, verbete, livro, capítulo, página, horário |
| `/My Clippings.txt` | destaques com título |
| `components/media/metric-card.h` | cartão de número pronto: rótulo, valor grande, unidade, legenda |
| `components/media/cover-grid.h` | grade de capas pronta, com provider, seleção e paginação |
| `src/components/HomeCoverCache.h` | decodificação de capa em PSRAM, **limite de 7 capas** |

Dois fatos que moldam o resto: uma sessão só é gravada com **10 segundos ou
mais e relógio acertado** (`src/activities/reader/ReaderSession.h:13`), e
`secs` é tempo de leitura ativa, não tempo com o livro aberto.

---

## Fase 0 — O agregador

Sem isto nada mais se sustenta: o jsonl nunca é truncado, e reler o arquivo
inteiro toda vez que a tela abre não escala.

**Arquivos novos:** `src/stats/StatsRollup.h` / `.cpp`.

**Produto:** `/.crosspoint/stats.bin`, versionado, reconstruível. A fonte da
verdade continua sendo o jsonl; o rollup é cache descartável.

Conferido: **Clear Reading Cache não apaga nenhum dos dois.**
`ClearCacheActivity` só remove diretórios cujo nome casa com
`isBookCacheDirectoryName` — prefixos `epub_`, `txt_`, `xtc_`
(`src/util/BookCacheUtils.cpp`). Arquivos soltos em `/.crosspoint` sobrevivem.

**Conteúdo do rollup:**

| Campo | Para quê | Custo |
| --- | --- | --- |
| `totalSecs`, `sessionCount` | tempo total | 8 B |
| `firstEpoch`, `lastEpoch` | "lendo desde" | 16 B |
| `minutesPerDay[730]` (u16) | mapa do mês, médias, sequência | 1,5 KB |
| `hourHistogram[24]` (u32) | horário favorito | 96 B |
| `longestSessionSecs` | recorde | 4 B |
| por livro: `docId`, `secs`, `firstStart`, `lastStart`, `maxTo`, `sessions` | aba Livros | 24 B × N |

Com teto de 512 livros são ~12 KB mais 1,6 KB de séries. Cabe folgado nos 8 MB
de PSRAM do X4 Classic e ainda caberia no C3 — o que importa se um dia você
quiser oferecer isso de volta.

**Onde enganchar:** ⚠ `src/sync/ReadingSessionLog.cpp`, dentro de
`readingSessionLog::append()`. É o **único** ponto de escrita de sessão no
firmware, então atualizar o rollup ali cobre todos os caminhos. Umas cinco
linhas.

**Reconstrução:** quando `stats.bin` não existe, a versão não bate, ou o jsonl
ficou maior do que o offset que o rollup registrou ter consumido. Guarde esse
offset no cabeçalho e o caso normal vira leitura incremental só da cauda.

---

## Fase 1 — O que é "livro terminado"

Toda contagem depende disto, e as três definições possíveis têm custos
diferentes:

| Definição | Custo | Problema |
| --- | --- | --- |
| Alguma sessão com `to >= 10000` | zero | pular para o fim conta como lido |
| Arquivo está em `/read` | zero, exige a opção ligada | mexer no cartão quebra |
| Flag gravada ao terminar | um store novo | nenhum |

**Vá de flag**, com a primeira como reconstrução do histórico que já existe.

**Arquivo novo:** `src/stats/FinishedBooksStore.h` / `.cpp`, um
`PersistableStore<FinishedBooksStore>` como `src/RecentBooksStore.h` já faz.
Grava em `/.crosspoint/finished.json`: `docId`, título, autor, idioma,
`finishedEpoch`, `secsSpent`.

**Por que um store próprio e não um arquivo dentro do cache do livro:** apagar
um livro apaga `.crosspoint/epub_<hash>/` inteiro (`removeBookFile()` em
`src/util/BookCacheUtils.cpp`). Um livro terminado e depois excluído tem de
continuar contando. Guardar título e autor na própria linha também deixa a aba
funcionar sem o arquivo original.

**Onde enganchar:** ⚠ `src/activities/reader/EpubReaderActivity.cpp:827`, onde
`pendingReadFolderMove` já decide que o livro acabou. Mesma condição, uma
chamada a mais.

---

## Fase 2 — A aba de estatísticas

**Arquivos novos:** `src/activities/stats/StatsActivity.h` / `.cpp` e
`src/activities/stats/StatsScreens.h` / `.cpp` (os desenhos, sem renderer, para
poderem ser testados no host).

`StatsActivity : UiTabListActivity` — a mesma base que Settings e Biblioteca
usam, então a navegação em anel por botões já vem pronta
(`src/activities/UiTabListActivity.h`). Quatro abas:

### Resumo
Seis `metricCard` em grade mais o mapa do mês:

- tempo total de leitura;
- livros terminados, com o número do ano ao lado;
- sequência de dias, atual e recorde;
- lido hoje e nesta semana;
- **terminando em ~X dias** — o livro atual, pela sua velocidade média nele;
- mapa do mês: grade de 7 colunas, um quadrado por dia, preenchimento conforme
  os minutos.

### Livros
É aqui que entram as capas. `coverGrid`
(`components/media/cover-grid.h`) com `itemProvider`, `columns = 3`,
`selectionIndicator` e paginação, alimentado pelo `FinishedBooksStore`. Cada
célula leva o rótulo com o tempo gasto. Confirmar numa capa abre o detalhe:
tempo no livro, quando começou e terminou, quantos dias levou, número de
sessões, duração média, velocidade, destaques e palavras consultadas.

⚠ **Limite real:** `HomeCoverCache::MAX_COVERS = 7`
(`src/components/HomeCoverCache.h:16`). Uma grade 3×3 precisa de 9. Ou você
pagina de 6 em 6 e reusa a cache como está, ou instancia uma segunda cache com
teto próprio. Paginar é mais barato e combina com e-ink, que não rola bem.

### Hábitos
Histograma de 24 horas a partir de `start + tz` — 24 barras desenhadas com
`target().fill()`, não precisa de biblioteca. Mais sessão mais longa, e a média
por dia em dois números: dividida pelos dias em que você leu e pelos dias
corridos. Mostrar os dois evita número enganoso.

### Biblioteca
No cartão / terminados / começados / nunca abertos. Autores mais lidos **por
tempo**, não por contagem — o `authorKey` do índice já junta "Victor Hugo" e
"Hugo Victor". Idiomas, vindos do `language` do `book.bin`. E duas linhas no
mesmo gráfico: adicionados por mês contra terminados por mês, que mostra se a
pilha de não-lidos está crescendo.

**Não há componente de gráfico no SDK**, e não faça um. O mapa do mês e o
histograma são retângulos; escreva um `statsui::` pequeno ao lado das telas.

**Entrada pelo Home:** ⚠ quatro pontos, todos pequenos —
`HomeMenuItem` em `src/activities/ActivityManager.h:21`,
`menuItemToIndex` e `indexToMenuItem` em `src/activities/home/HomeActivity.h:49,66`,
e o vetor `menuItems` em `src/activities/home/HomeActivity.cpp:556`.

**Uma estatística que eu não faria:** páginas lidas. Página depende de fonte,
tamanho e orientação, muda quando você mexe nos ajustes, e o número vira
mentira. Porcentagem e caracteres são estáveis.

---

## Fase 3 — Gamificação

Em leitura, o que funciona é o que reflete hábito. Ponto arbitrário não.

**Arquivo novo:** `src/stats/AchievementStore.h` / `.cpp`, guardando apenas
`id → unlockedEpoch` em `/.crosspoint/achievements.json`. A condição de cada
conquista é uma função pura do rollup, avaliada na mesma chamada que o atualiza
— nada de varrer nada em tela.

**A mecânica central é a sequência de dias**, que a Fase 0 já calcula.

**Metas**, as duas configuráveis: minutos por dia e livros por ano. Desenhadas
com `progressBar` (`components/controls/progress-bar.h`).

**Conquistas**, todas deriváveis do que já existe: primeiro livro terminado;
10, 25, 50 e 100 livros; 10, 50 e 100 horas; sequência de 7, 30 e 100 dias; um
livro inteiro num só dia; leitura em três idiomas; 100 palavras consultadas;
100 destaques.

Três regras de desenho que valem mais que a lista:

**Nada de pop-up durante a leitura.** E-ink pisca a tela inteira e tira a pessoa
do livro — é o oposto do que o aparelho existe para fazer. Conquista aparece na
tela de fim de livro, na aba, ou na tela de descanso.

**Sequência que zera e pune faz abandonar.** Dê uma folga por semana antes de
quebrar a sequência, e guarde o recorde separado do atual.

**Nenhum número inventado.** Se não dá para derivar do log, não entra. "Palavras
por minuto" só com a ressalva de estimativa, porque sai de `cumulativeSize`
dividido por ~5,5 caracteres por palavra.

---

## Fase 4 — Tela de descanso com o resumo

⚠ Três pontos no upstream, todos pequenos:

1. `src/CrossPointSettings.h:22` — `STATS = 9` antes de
   `SLEEP_SCREEN_MODE_COUNT`. O enum é persistido por valor, então o novo vai no
   fim; não reordene os existentes ou todo mundo troca de tela de sono.
2. `src/activities/boot_sleep/SleepActivity.cpp:747` — um `case` no switch,
   chamando um `renderStatsSleepScreen()` novo.
3. A lista de valores do setting e uma string nova `STR_SLEEP_STATS`, em
   `english.yaml` e `portuguese-BR.yaml`.

**Conteúdo:** quatro a seis números grandes e o mapa do mês. Nada mais — é uma
tela que se olha de relance.

**A restrição que decide o desenho:** isto roda no caminho de desligar, com
orçamento apertado de tempo e de RAM. Ler `stats.bin`, que são poucos KB, é
aceitável. Recalcular qualquer coisa a partir do jsonl, não. Se o rollup não
existir no momento de dormir, caia para o modo padrão em vez de construí-lo.

**Variante opcional `COVER_STATS = 10`:** capa do livro atual em cima, faixa de
estatísticas embaixo. O caminho da capa já existe em `renderCoverSleepScreen()`.

---

## Fase 5 — Verificação

O agregador é lógica pura sobre um arquivo de texto, então **roda no host, sem
aparelho**. É aí que as estatísticas quebram, e é barato cobrir:

Suíte nova com jsonl sintético, cobrindo sessões fora de ordem cronológica,
`start` igual a zero (relógio nunca acertado), fuso negativo, sessão cruzando a
meia-noite, dois livros com o mesmo título, livro apagado depois de terminado,
`to` menor que `from`, e um jsonl truncado no meio de uma linha por queda de
energia.

A parte visual vai no aparelho. Teste em hardware é responsabilidade sua e,
pelas regras deste repositório, obrigatório.

---

## Ordem, tamanho e onde encosta no upstream

| Fase | Arquivos novos | Linhas em arquivo do upstream |
| --- | --- | --- |
| 0 Agregador | `src/stats/StatsRollup.*` | ⚠ `ReadingSessionLog.cpp` (~5) |
| 1 Terminados | `src/stats/FinishedBooksStore.*` | ⚠ `EpubReaderActivity.cpp:827` (~3) |
| 2 Aba | `src/activities/stats/*` | ⚠ `ActivityManager.h:21`, `HomeActivity.h:49,66`, `HomeActivity.cpp:556` (~10) |
| 3 Gamificação | `src/stats/AchievementStore.*` | nenhuma |
| 4 Tela de sono | — | ⚠ `CrossPointSettings.h:22`, `SleepActivity.cpp:747`, 2 YAMLs (~8) |
| 5 Testes | `test/stats_rollup/` | nenhuma |

Cerca de 26 linhas em código do upstream no total. É esse número que mantém o
fork capaz de puxar as novidades deles sem conflito.

Faça 0 e 1 juntos e pare: sem agregador confiável e sem definição de terminado,
as fases seguintes desenham números errados com muito capricho.

## Regras da casa

Tudo isto muda o firmware, então passa pelo `firmware-handoff` antes de ser
declarado pronto. Commit local só com sua aprovação explícita, `push` só com
instrução explícita, e a descrição de qualquer PR é você quem escreve.
