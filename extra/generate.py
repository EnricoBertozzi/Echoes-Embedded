#!/usr/bin/env python3
"""Pré-build: garante que exista um private.pem do dispositivo em data/.

Gerado uma única vez (o arquivo é gitignored e sobrevive entre builds);
o firmware carrega esse arquivo no LittleFS via `uploadfs` e o usa como
identidade ES256 (ECDSA P-256) para o autocadastro na plataforma e para
assinar os JWTs do dispositivo (device-api.md).

Uso:
    python3 extra/generate.py            # gera data/private.pem se não existir
    python3 extra/generate.py --force    # regenera (cuidado: o servidor não
                                         # aceita troca de chave para o mesmo MAC)

Não gera pub.pem nem private.bin: a chave pública o próprio dispositivo
exporta da privada ao se registrar. Se precisar dela manualmente:
    openssl ec -in data/private.pem -pubout
"""

import os
import subprocess
import sys
from pathlib import Path

try:
    PROJECT_ROOT = Path(__file__).resolve().parent.parent
except NameError:
    # Executando como extra_script do PlatformIO: os globals do SCons não
    # definem __file__, mas o CWD é a raiz do projeto.
    PROJECT_ROOT = Path.cwd()

PRIVATE_PEM = PROJECT_ROOT / "data" / "private.pem"


def generate_pem():
    """Chave EC P-256 em PEM SEC1 (mesmo formato que o mbedtls grava no ESP)."""
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ec

        key = ec.generate_private_key(ec.SECP256R1())
        return key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=serialization.NoEncryption(),
        )
    except ImportError:
        pass

    try:
        return subprocess.run(
            ["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout"],
            check=True,
            capture_output=True,
        ).stdout
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        print(
            "AVISO: nem a lib 'cryptography' nem o openssl estão disponíveis; "
            "data/private.pem não foi gerado (o dispositivo gerará a própria "
            "chave no primeiro boot, se necessário)."
        )
        print(f"       ({exc})")
        return None


def main() -> None:
    force = "--force" in sys.argv

    if PRIVATE_PEM.exists() and not force:
        print("extra/generate.py: data/private.pem já existe — ignorando")
        return

    pem = generate_pem()
    if pem is None:
        return

    PRIVATE_PEM.parent.mkdir(parents=True, exist_ok=True)
    with open(PRIVATE_PEM, "wb") as fh:
        fh.write(pem)
    os.chmod(PRIVATE_PEM, 0o600)

    print(f"extra/generate.py: data/private.pem gerado ({len(pem)} bytes)")


main()
