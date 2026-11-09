#!/usr/bin/env python3
"""Extrai o private.pem de um dump da partição LittleFS do Echoes.

Recuperação sem firmware especial (alternativa aos comandos `dump`/`restore`
do console serial):

1. Faça o dump da partição spiffs (offset e tamanho do partitions.csv):
     esptool.py --port /dev/ttyUSB0 read_flash 0x21E000 0x4B000 spiffs.bin
   (ou: python3 -m esptool --port ... read_flash ...)

2. Extraia a chave:
     python3 tools/extract_private_key.py spiffs.bin [saida.pem]

Requer: pip install littlefs-python
"""
import sys

from littlefs import LittleFS
from littlefs.context import UserContext

BLOCK_SIZE = 4096


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)

    dump = sys.argv[1]
    output = sys.argv[2] if len(sys.argv) > 2 else "private.pem"

    with open(dump, "rb") as fh:
        image = bytearray(fh.read())
    if len(image) % BLOCK_SIZE:
        print(f"aviso: tamanho {len(image)} não é múltiplo de {BLOCK_SIZE}")

    context = UserContext(buffer=image)
    fs = LittleFS(
        context=context,
        block_size=BLOCK_SIZE,
        block_count=len(image) // BLOCK_SIZE,
        mount=False,
    )
    fs.mount()

    names = fs.listdir("/")
    if "private.pem" not in names:
        print(f"private.pem não encontrado; arquivos na raiz: {names}")
        sys.exit(1)

    with fs.open("/private.pem", "rb") as fh:
        pem = fh.read()

    with open(output, "wb") as fh:
        fh.write(pem)
    print(f"{output} salvo ({len(pem)} bytes)")
    print("chave pública correspondente: openssl ec -in private.pem -pubout")


if __name__ == "__main__":
    main()
