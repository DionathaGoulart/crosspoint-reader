# Plano: dicionário pt-BR e interface 100% em português

Duas trilhas independentes. A trilha A não toca no firmware e vale para qualquer
build já instalado. A trilha B muda o binário, então exige recompilar e regravar.

Tudo abaixo foi lido do código deste repositório, na branch `develop`, SDK pinado
em `995d9bd523b9c45b18710bca17a3158ce724a7f4` (ainda não inicializado neste clone).

---

## Passo zero, para as duas trilhas

O submódulo está vazio. Nada compila antes disto:

```bash
cd /Users/rafael/Desktop/.good/crosspoint-reader
git submodule update --init --recursive
```

---

## Trilha A — dicionário em português no cartão

### A.1 O que o firmware aceita

Lido do carregador, não da especificação StarDict:

| Requisito | Onde está escrito |
| --- | --- |
| Uma pasta por dicionário; duas raízes `.idx` na mesma pasta e a pasta inteira é ignorada | `src/util/DictionaryRegistry.h:15-18` |
| `.idx` obrigatório e **descompactado**; `.idx.gz` não é lido | `docs/dictionary.md` |
| `.dict` ou `.dict.dz` — o `.dz` é descompactado verbete a verbete | `src/util/Dictionary.cpp` |
| `.ifo` opcional; `idxoffsetbits=64` faz o dicionário ser **rejeitado** | `src/util/Dictionary.cpp:85-90` |
| `sametypesequence=h` ou `x` → definição em HTML/XDXF, convertida para texto puro | `src/util/Dictionary.cpp:92-94` |
| `.syn` opcional; é ele que resolve formas alternativas para o verbete | `docs/dictionary.md` |
| Raízes de busca: `/dictionaries/<pasta>/` e `/.dictionaries/<pasta>/` | `src/util/DictionaryRegistry.h:20-23` |

### A.2 A limitação que importa para o português

A busca tem quatro passos: **direta → sinônimos (`.syn`) → stemming → não achou**.
O terceiro passo só conhece inglês — plurais em `-s`, possessivos, `-ed`, `-ing`.
Não existe regra para `-ões`, `-ávamos`, `-íssimo`.

Consequência prática: *"corríamos"* só encontra *"correr"* se o próprio índice
listar a forma conjugada, ou se houver um `.syn` que aponte para ela. Prefira um
dicionário que traga `.syn`.

### A.3 Fonte verificada

**Wiktionary StarDict** — <https://xxyzz.github.io/wiktionary_stardict/>, geração
`20261115`. Os arquivos ficam em
`https://github.com/xxyzz/wiktionary_stardict/releases/download/20261115/`.

| Arquivo | Conteúdo | Verbetes | Tamanho |
| --- | --- | --- | --- |
| `pt-pt.tar.zst` | Português monolíngue (edição portuguesa do Wiktionary) | 95.988 | 16 MB |
| `pt-en.tar.zst` | Português → inglês | 75.195 | 14 MB |
| `en-pt.tar.zst` | Inglês definido em português | 15.270 | 1 MB |

O `pt-pt` aqui é a *edição* do Wiktionary em português, não a variante de
Portugal; a página não rotula nenhum arquivo como pt-BR.

**Recomendado:** `pt-pt.tar.zst`, monolíngue, para ler livros em português.
Opcionalmente `en-pt.tar.zst` numa segunda pasta, para livros em inglês.

`download.huzheng.org`, a outra fonte clássica, recusou conexão
(`ECONNREFUSED 172.104.67.108:443`) nas duas tentativas. Não conte com ela.

Nenhum dos arquivos acima foi aberto. O `.ifo` e o `idxoffsetbits` só se
confirmam depois de extrair — é o que o passo seguinte faz.

### A.4 Preparar e conferir

```bash
mkdir -p /tmp/dict-pt && cd /tmp/dict-pt
# baixe pt-pt.tar.zst para esta pasta, depois:
tar --zstd -xf pt-pt.tar.zst          # ou: zstd -d pt-pt.tar.zst && tar xf pt-pt.tar
python3 /Users/rafael/Desktop/.good/crosspoint-reader/scripts_local/check_stardict.py /tmp/dict-pt/<pasta-extraida>
```

`scripts_local/check_stardict.py` aplica exatamente as regras da tabela A.1:
índice único, `.idx` não-gzip, dados presentes, `idxoffsetbits`, coerência entre
`idxfilesize` e o tamanho real, e avisa quando não há `.syn`.

Se o `.idx` vier compactado:

```bash
mv dicionario.idx dicionario.idx.gz && gzip -d dicionario.idx.gz
```

### A.5 Instalar

1. Copie a pasta para `/dictionaries/portugues/` no microSD. Use
   `/.dictionaries/portugues/` se quiser escondê-la do navegador de arquivos.
2. No aparelho: **Settings → Reader → Dictionary → portugues**. A linha só
   aparece quando existe pelo menos uma pasta válida.
3. A primeira consulta mostra *"Indexing dictionary…"* e grava `.qidx` e `.sidx`
   ao lado. Leva alguns segundos; as seguintes são rápidas. Os dois podem ser
   apagados a qualquer momento — são reconstruídos.

### A.6 Testar no aparelho

Abra um EPUB em português, **Confirm → Look Up**, e consulte quatro palavras
escolhidas de propósito:

1. uma palavra simples com acento — valida UTF-8 e o recorte de pontuação;
2. um substantivo no plural — mede o buraco do stemming;
3. um verbo conjugado — idem, pior caso;
4. uma locução de duas palavras — o índice casa frases com espaço.

Anote quais falharam. Esse é o tamanho real do problema, não uma estimativa.

### A.7 Se o buraco do stemming incomodar

Duas saídas, em ordem de custo:

- **Gerar um `.syn` em português.** Mapeia forma flexionada → verbete, fora do
  aparelho, a partir de uma lista morfológica (o dicionário Hunspell pt-BR do
  LibreOffice serve de base). Não toca no firmware e vale para qualquer build.
- **Adicionar regras de stemming português ao firmware**, em
  `src/util/Dictionary.cpp`. Mexe no binário, logo cai nas regras de handoff do
  repositório (ver "Regras da casa" no fim). É candidato legítimo a PR upstream:
  o projeto já carrega hifenização portuguesa, então a língua não é estranha ao
  escopo.

Faça a primeira só se o teste A.6 mostrar que vale a pena.

---

## Trilha B — interface 100% em pt-BR

### B.1 Estado medido

Os idiomas deste firmware são **compilados no binário**, não carregados do
cartão: `custom_i18n_builtin_langs = all` em `platformio.ini:136`, e
`lib/I18n/` não tem nenhum carregador de pacote. Português do Brasil já está lá;
o que falta é texto.

| | |
| --- | --- |
| Chaves em `english.yaml` | 669 |
| Chaves em `portuguese-BR.yaml` | 522 |
| **Faltando** | **147** (78% traduzido) |
| Caracteres a escrever | 3.071 |
| Dessas 147, já existem em `portuguese-PT.yaml` | 0 |
| Dessas 147, já existem em `spanish.yaml` | 134 |

Português de Portugal está atrás exatamente nas mesmas 147 — não há atalho por
ali. O espanhol cobre 134 e serve de referência de sentido, não de tradução.

Onde o buraco se concentra:

| Prefixo | Chaves | Tela |
| --- | --- | --- |
| `STR_BT_*` | 26 | Bluetooth page turner |
| `STR_OPDS_*` | 21 | catálogos OPDS |
| `STR_PLUGIN_*` | 16 | plugins |
| `STR_SPACING_*` | 7 | espaçamento do leitor |
| `STR_CLIPPING_*` | 5 | clippings e sincronização |
| `STR_EVENT_*` | 5 | eventos de plugin |
| `STR_HAPTIC_*`, `STR_VIBRATION_*` | 7 | vibração |
| `STR_FEED_*` | 4 | erros de feed |
| `STR_LCP_*` | 4 | livros protegidos |
| `STR_KOREADER_*` | 4 | sincronização |
| resto | 48 | avulsas |

### B.2 A planilha já está pronta

`scripts_local/faltando-ptbr.yaml` traz as 147 na ordem do arquivo inglês. Cada
uma vem com a linha em inglês e a em espanhol comentadas acima, e a linha a
preencher embaixo, já no formato final:

```yaml
# EN  STR_FONT_DAMAGED: "Damaged font file"
# ES  STR_FONT_DAMAGED: "Archivo de fuente dañado"
STR_FONT_DAMAGED: "Damaged font file"
```

### B.3 Traduzir

Regras que o gerador e a tela impõem (`docs/i18n.md`):

- UTF-8, uma chave por linha, no formato `STR_CHAVE: "valor"`, valor sempre
  entre aspas.
- **Espaços no início e no fim dentro das aspas são significativos.** Vários
  valores são prefixos que o código cola a um número, uma versão ou um nome.
  Apagar um espaço junta duas palavras na tela.
- `\n` para quebra de linha, `\\` para barra, `\"` para aspas dentro do valor.
- Texto curto. É tela de e-ink, e a maioria dessas chaves é rótulo de linha de
  menu ou de botão.
- Não traduza `STR_CROSSPOINT` nem nomes de protocolo (OPDS, KOReader, LCP).
- Termo igual em todo lugar: decida uma vez entre *"Sincronizar"* e
  *"Sincronização"*, entre *"Marcador"* e *"Favorito"*, e mantenha.

Cole as linhas traduzidas no fim de `lib/I18n/translations/portuguese-BR.yaml`.
A ordem dentro do arquivo não importa; o gerador indexa por chave.

### B.4 Verificar sem compilar

```bash
python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/
```

O gerador preenche com inglês o que faltar **e avisa**. Zero avisos de
`portuguese-BR` é a definição operacional de 100%. Conferência independente:

```bash
python3 - <<'EOF'
import re, pathlib
k = lambda p: {m.group(1) for line in pathlib.Path(p).read_text(encoding='utf-8').splitlines()
               if (m := re.match(r'^([A-Za-z0-9_]+):', line)) and not m.group(1).startswith('_')}
en = k('lib/I18n/translations/english.yaml')
pt = k('lib/I18n/translations/portuguese-BR.yaml')
print("faltando:", len(en - pt))
EOF
```

`gen_i18n.py` também roda sozinho antes de cada build (`platformio.ini:142`,
`pre:scripts/gen_i18n.py`), então o comando acima é só para checar rápido.

**Não versione os arquivos gerados.** `lib/I18n/I18nKeys.h`,
`I18nStrings.h` e `I18nStrings.cpp` estão no `.gitignore` (linhas 12-14). O
único arquivo que muda nesta trilha é `portuguese-BR.yaml`.

### B.5 Compilar e gravar

```bash
pio run -e x4c-gh_release
```

`x4c-gh_release` é o alvo certo: o `x4c` de desenvolvimento carrega
`-DCROSSPOINT_WAIT_FOR_USB_SERIAL` (`platformio.ini:495`), que espera a serial
USB no boot — ruim num aparelho cujo cabo só carrega.

### B.6 Conferir na tela

Idioma em **Settings → System → Language → Português (Brasil)**, depois percorra
só as telas que ganharam texto novo, na ordem da tabela B.1:

Bluetooth page turner · catálogos OPDS (incluindo os erros de feed) · plugins ·
espaçamento no menu do leitor · clippings e sincronização · vibração ·
sincronização KOReader · livro protegido por LCP.

Procure três defeitos: texto cortado na largura, texto que vazou para fora do
botão, e palavras coladas — esta última quase sempre é um espaço apagado dentro
das aspas (ver B.3).

### B.7 Opcional: devolver para o upstream

A tradução interessa a mais gente, e o projeto já credita tradutores em
`docs/translators.md` (Português do Brasil tem dois nomes hoje). Um PR com
`portuguese-BR.yaml` mais o seu nome no arquivo de créditos é pequeno e bem
recebido. As regras do repositório pedem que **a descrição do PR seja escrita
por você**, não gerada.

---

## Ordem de execução

A trilha A dá resultado hoje, sem recompilar nada. A trilha B precisa de build
e regravação. Então:

1. Passo zero (submódulo).
2. A.3 → A.5: dicionário no cartão, já usável.
3. A.6: teste das quatro palavras.
4. B.2 → B.4: traduzir e verificar.
5. B.5 → B.6: um único build e uma única regravação, com o dicionário já validado.
6. A.7 e B.7 só depois, se os testes mostrarem que valem.

Juntar a regravação da trilha B com qualquer ajuste de firmware da A.7 evita
gravar o aparelho duas vezes.

---

## Regras da casa

O `CLAUDE.md` deste repositório é explícito, e vale para este plano:

- Tradução **altera o firmware**, então a trilha B passa pelo
  `firmware-handoff` antes de ser declarada pronta.
- O script e esta página são host-only e documentação: revisão mais leve.
- Commits locais só com a sua aprovação explícita; `push` só com instrução
  explícita de empurrar. Nada foi commitado.
- Teste em hardware é responsabilidade humana e é obrigatório antes de abrir PR.
