#!/usr/bin/env python3
"""Check a StarDict folder against what this firmware actually accepts.

The rules below are not from the StarDict spec; they are read off the reader's
own loader, so a folder that passes here is one the device will open:

  src/util/DictionaryRegistry.h   one .idx stem per folder, or it is skipped
  src/util/Dictionary.cpp:85      idxoffsetbits=64 is rejected
  src/util/Dictionary.cpp:92      sametypesequence h/x means HTML/XDXF
  docs/dictionary.md              .idx must be uncompressed; .dict.dz is fine

Usage:
    python3 scripts_local/check_stardict.py <pasta-do-dicionario> [...]
"""
import pathlib
import sys

GZIP_MAGIC = b"\x1f\x8b"


def fail(messages, text):
    messages.append(("FALHA", text))


def warn(messages, text):
    messages.append(("AVISO", text))


def ok(messages, text):
    messages.append(("ok", text))


def read_ifo(path, messages):
    """Parse the .ifo the way readIfoFacts does: key=value, one per line."""
    facts = {}
    try:
        raw = path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        warn(messages, f".ifo ilegivel ({error}); o firmware segue sem ele")
        return facts
    for line in raw.splitlines():
        line = line.rstrip("\r")
        if "=" not in line:
            continue
        key, _, value = line.partition("=")
        facts[key.strip()] = value.strip()
    return facts


def check_folder(folder):
    messages = []
    folder = pathlib.Path(folder)
    if not folder.is_dir():
        return [("FALHA", f"{folder} nao e uma pasta")]

    # One dictionary per folder. DictionaryRegistry::discover skips a folder
    # with more than one .idx stem as ambiguous, without saying why.
    idx_files = sorted(folder.glob("*.idx"))
    if not idx_files:
        fail(messages, "nenhum arquivo .idx na pasta")
        if sorted(folder.glob("*.idx.gz")):
            fail(messages, ".idx.gz encontrado: descompacte com `gzip -d` (o firmware nao le .gz)")
        return messages
    if len(idx_files) > 1:
        stems = ", ".join(p.stem for p in idx_files)
        fail(messages, f"{len(idx_files)} indices na mesma pasta ({stems}); "
                       "a pasta inteira e ignorada. Um dicionario por pasta.")
        return messages

    idx = idx_files[0]
    stem = idx.with_suffix("").name
    ok(messages, f"indice: {idx.name}")

    # A .idx that is still gzip is the most common failure: the extension says
    # .idx but the bytes are a gzip member, and the loader reads garbage.
    with idx.open("rb") as handle:
        if handle.read(2) == GZIP_MAGIC:
            fail(messages, f"{idx.name} esta compactado em gzip apesar do nome; "
                           "renomeie para .idx.gz e rode `gzip -d`")

    dict_plain = folder / f"{stem}.dict"
    dict_dz = folder / f"{stem}.dict.dz"
    if dict_plain.exists():
        ok(messages, f"dados: {dict_plain.name} ({dict_plain.stat().st_size:,} bytes)")
    elif dict_dz.exists():
        ok(messages, f"dados: {dict_dz.name} ({dict_dz.stat().st_size:,} bytes), "
                     "descompactado sob demanda")
    else:
        fail(messages, f"falta {stem}.dict ou {stem}.dict.dz")

    syn = folder / f"{stem}.syn"
    if syn.exists():
        ok(messages, f"sinonimos: {syn.name} — formas flexionadas resolvem para o verbete")
    else:
        warn(messages, "sem .syn: em portugues, plurais e conjugacoes so acham o verbete "
                       "se o proprio indice os listar (o stemming automatico e so de ingles)")

    ifo = folder / f"{stem}.ifo"
    if not ifo.exists():
        warn(messages, f"sem {stem}.ifo: o firmware assume tipos inline e segue")
        return messages

    facts = read_ifo(ifo, messages)
    bits = facts.get("idxoffsetbits")
    if bits is not None and bits.strip().isdigit() and int(bits) == 64:
        fail(messages, "idxoffsetbits=64 — este dicionario e rejeitado pelo firmware")
    elif bits is not None:
        ok(messages, f"idxoffsetbits={bits}")

    types = facts.get("sametypesequence")
    if types in ("h", "x"):
        ok(messages, f"sametypesequence={types}: definicoes em HTML/XDXF, "
                     "convertidas para texto puro na tela")
    elif types:
        ok(messages, f"sametypesequence={types}")
    else:
        ok(messages, "sem sametypesequence: tipos inline por verbete")

    count = facts.get("wordcount")
    if count:
        ok(messages, f"wordcount={count}")

    declared = facts.get("idxfilesize")
    if declared and declared.strip().isdigit():
        actual = idx.stat().st_size
        if int(declared) != actual:
            fail(messages, f"idxfilesize={declared} no .ifo, mas {idx.name} tem {actual} bytes; "
                           "o indice provavelmente foi recompactado ou truncado")
        else:
            ok(messages, f"idxfilesize confere ({actual:,} bytes)")

    return messages


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    worst = 0
    for folder in argv[1:]:
        print(f"\n=== {folder}")
        for level, text in check_folder(folder):
            print(f"  {level:6} {text}")
            if level == "FALHA":
                worst = 1
    print()
    print("Pronto para o cartao." if worst == 0 else "Corrija as FALHAs antes de copiar.")
    return worst


if __name__ == "__main__":
    sys.exit(main(sys.argv))
