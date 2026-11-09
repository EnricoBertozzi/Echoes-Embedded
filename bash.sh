#!/usr/bin/env bash
# Cadastro automatico do ESP - FIRMWARE (Echoes-Embedded)
#
# uso, na raiz do firmware (onde esta o platformio.ini):
#   bash aplicar-cadastro-firmware.sh              aplica as correcoes e confere
#   bash aplicar-cadastro-firmware.sh --simular    mostra o que faria, sem alterar nada
#   bash aplicar-cadastro-firmware.sh --compilar   aplica e roda pio run (se o pio existir)
#
# o que faz:
#   1 DeviceRegister.cpp e DeviceAuthenticator.cpp passam a enviar hardware_id
#     (payload do registro e claim do JWT); o servidor ainda aceita mac
#   2 o topico do ack aceita {hardware_id} e {mac}
#   3 troca {mac} por {hardware_id} no MQTT_ACK_TOPIC do .env.example e do .env
# o que foi alterado fica salvo em .backup-cadastro-<data>/ (pode ser rodado de novo)
set -euo pipefail

SIMULAR=0
COMPILAR=0
for a in "$@"; do
  case "$a" in
    --simular) SIMULAR=1 ;;
    --compilar) COMPILAR=1 ;;
    *) echo "opcao desconhecida: $a"; exit 1 ;;
  esac
done

if [[ ! -f platformio.ini || ! -f src/main.cpp ]]; then
  echo "rode na raiz do firmware (onde esta o platformio.ini)"
  exit 1
fi
if [[ ! -f src/application/service/DeviceRegister.cpp || ! -f src/infra/security/DeviceAuthenticator.cpp ]]; then
  echo "DeviceRegister.cpp ou DeviceAuthenticator.cpp nao encontrados: este firmware ainda nao tem o cadastro automatico"
  exit 1
fi

for cmd in tar base64 sed grep cmp; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "falta o comando: $cmd"; exit 1; }
done

BK=".backup-cadastro-$(date +%Y%m%d-%H%M%S)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
MUDOU=0

salvar() {
  local f="$1"
  [[ -f "$f" ]] || return 0
  [[ $SIMULAR -eq 1 ]] && return 0
  mkdir -p "$BK/$(dirname "$f")"
  cp -p "$f" "$BK/$f"
}

echo "1 aplicando os arquivos corrigidos"
sed -n '/^__PAYLOAD__$/,$p' "$0" | tail -n +2 | { base64 -d 2>/dev/null || base64 -D; } | tar -xz -C "$TMP"

while IFS= read -r rel; do
  rel="${rel#./}"
  src="$TMP/$rel"
  if [[ -f "$rel" ]] && cmp -s "$src" "$rel"; then
    continue
  fi
  echo "  copiar $rel"
  MUDOU=1
  salvar "$rel"
  if [[ $SIMULAR -eq 0 ]]; then
    mkdir -p "$(dirname "$rel")"
    cp "$src" "$rel"
  else
    echo "  [simular] cp $rel"
  fi
done < <(cd "$TMP" && find . -type f | sort)

echo "2 ajustando MQTT_ACK_TOPIC nos arquivos de ambiente"
for envf in .env.example .env; do
  if [[ -f "$envf" ]] && grep -qE '^MQTT_ACK_TOPIC=.*\{mac\}' "$envf"; then
    echo "  $envf: {mac} -> {hardware_id}"
    MUDOU=1
    salvar "$envf"
    if [[ $SIMULAR -eq 0 ]]; then
      sed -i.tmp -E '/^MQTT_ACK_TOPIC=/ s/\{mac\}/{hardware_id}/' "$envf"
      rm -f "$envf.tmp"
    fi
  fi
done
if [[ ! -f .env ]]; then
  echo "  aviso: .env nao encontrado (copie o .env.example e preencha API_HOST e MQTT_*)"
fi

if [[ $SIMULAR -eq 1 ]]; then
  echo "simulacao terminada, nada foi alterado"
  exit 0
fi

echo "3 conferindo"
ERROS=0
grep -q 'doc\["hardware_id"\]' src/application/service/DeviceRegister.cpp \
  || { echo "  DeviceRegister.cpp nao envia hardware_id"; ERROS=$((ERROS + 1)); }
grep -q '{hardware_id}' src/application/service/DeviceRegister.cpp \
  || { echo "  DeviceRegister.cpp nao trata {hardware_id} no topico do ack"; ERROS=$((ERROS + 1)); }
grep -q '\\"hardware_id\\"' src/infra/security/DeviceAuthenticator.cpp \
  || { echo "  DeviceAuthenticator.cpp nao usa a claim hardware_id"; ERROS=$((ERROS + 1)); }
if [[ -f .env ]]; then
  for var in API_HOST MQTT_BROKER_URL MQTT_USERNAME MQTT_PASSWORD; do
    grep -qE "^${var}=.+" .env || echo "  aviso: $var vazio ou ausente no .env"
  done
fi
if [[ $ERROS -gt 0 ]]; then
  echo "conferencia encontrou $ERROS problema(s); backup em $BK"
  exit 1
fi
echo "  ok: payload, claim do JWT e topico do ack usam hardware_id"

if [[ $COMPILAR -eq 1 ]]; then
  if command -v pio >/dev/null 2>&1; then
    echo "4 compilando (pio run -e esp32)"
    pio run -e esp32
  else
    echo "  pio nao encontrado, pulei a compilacao"
  fi
fi

echo
if [[ $MUDOU -eq 1 ]]; then
  echo "pronto. backup em $BK"
else
  echo "pronto. nada precisou mudar (ja estava aplicado)"
fi
echo "lembrete: a chave privada do ESP e gerada no primeiro boot e fica salva na flash;"
echo "  o servidor guarda a chave publica por hardware_id, entao so apague a flash do ESP"
echo "  se tambem excluir o dispositivo no servidor (senao o registro vem como outra chave)"
exit 0
__PAYLOAD__
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
NdVU01fRfwBWrekeAFAAAA==
