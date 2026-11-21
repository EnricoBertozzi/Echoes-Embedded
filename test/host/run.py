#!/usr/bin/env python3
"""Focused host regressions; run after `pio run -e esp32` installs ArduinoJson."""
import base64
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

ROOT = Path(__file__).resolve().parents[2]
HOST = ROOT / "test" / "host"
ROOT = HOST.parents[1]


def run(*args, **kwargs):
    return subprocess.check_output(list(map(str, args)), text=True, cwd=ROOT, **kwargs)


def decode(value):
    return base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))


def verify_jwt(output):
    fields = dict(line.split("=", 1) for line in output.splitlines() if "=" in line)
    token = fields["TOKEN"]
    header, payload, signature = token.split(".")
    assert json.loads(decode(header)) == {"alg": "ES256", "typ": "JWT"}
    assert json.loads(decode(payload)) == {
        "iss": "echoes-device", "hardware_id": "AA:bb:CC:01:02:03",
        "iat": 1795176000, "exp": 1795176120,
    }
    raw = decode(signature)
    assert len(raw) == 64 and "=" not in signature
    der = utils.encode_dss_signature(int.from_bytes(raw[:32], "big"), int.from_bytes(raw[32:], "big"))
    public = serialization.load_pem_public_key(decode(fields["PUBLIC"]))
    signed = f"{header}.{payload}".encode()
    public.verify(der, signed, ec.ECDSA(hashes.SHA256()))
    for key, message in [
        (public, signed + b"x"),
        (ec.generate_private_key(ec.SECP256R1()).public_key(), signed),
    ]:
        try:
            key.verify(der, message, ec.ECDSA(hashes.SHA256()))
            raise AssertionError("invalid signature accepted")
        except InvalidSignature:
            pass
    print("PASS: JWT ES256 verified independently; claims, single clock read, tamper/wrong-key rejection")


def main():
    json_include = Path(os.environ.get(
        "ARDUINOJSON_INCLUDE", ROOT / ".pio/libdeps/esp32/ArduinoJson/src"))
    # This is a compile dependency only; no SDK/build tree is scanned.
    assert (json_include / "ArduinoJson.h").is_file(), "Run pio run -e esp32 first"
    with tempfile.TemporaryDirectory(prefix="echoes-host-", dir="/tmp/opencode") as tmp:
        tmp = Path(tmp)
        compiler = [
            os.environ.get("CXX", "g++"), "-std=c++17", "-g", "-O1", "-Wall", "-Wextra",
            "-DARDUINOJSON_ENABLE_ARDUINO_STRING=1",
            "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0", "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0",
            f"-I{HOST / 'fakes'}", f"-I{ROOT / 'include'}", f"-I{json_include}", f"-I{tmp}",
        ]
        sources = [
            HOST / "contracts.cpp", ROOT / "src/application/security/JwtBuilder.cpp",
            ROOT / "src/infra/security/DeviceAuthenticator.cpp",
            ROOT / "src/application/service/ScenarioSyncService.cpp",
            ROOT / "src/infra/hardware/EspAudioPlayer.cpp",
        ]
        run(*compiler, *sources, "-Wl,--wrap=time", "-lcrypto", "-pthread", "-o", tmp / "contracts")
        output = run(tmp / "contracts")
        verify_jwt(output)
        print("\n".join(line for line in output.splitlines() if line.startswith("PASS:")))

        # Compile the unchanged pure helper from DeviceRegister.cpp with a runtime template.
        register = (ROOT / "src/application/service/DeviceRegister.cpp").read_text()
        helper = register.split("std::string ackTopicFor(", 1)[1].split("\n} // namespace", 1)[0]
        default = re.search(r'#define MQTT_ACK_TOPIC ("[^"]+")', register).group(1)
        (tmp / "ack.cpp").write_text(
            '#include <string>\n#include <cassert>\n#include <iostream>\n'
            'std::string ackTemplate;\n#define MQTT_ACK_TOPIC ackTemplate\n'
            'std::string ackTopicFor(' + helper + '\nint main() {\n'
            'const std::string mac = "AA:bb:CC:01:02:03";\n'
            f'ackTemplate = {default}; assert(ackTopicFor(mac) == "register/ack/" + mac);\n'
            'for (auto marker : {"{hardware_id}", "{hardwareId}", "{mac}"}) {\n'
            'ackTemplate = std::string("ack/") + marker;\n'
            'assert(ackTopicFor(mac) == "ack/" + mac); }\n'
            'ackTemplate = "{hardwareId}/{mac}/{hardware_id}/{hardwareId}";\n'
            'assert(ackTopicFor(mac) == mac + "/" + mac + "/" + mac + "/" + mac);\n'
            'ackTemplate = "fixed/topic"; assert(ackTopicFor(mac) == "fixed/topic");\n'
            'std::cout << "PASS: ACK default, all three markers, repeated/mixed/literal templates\\n"; }\n')
        run(*compiler, tmp / "ack.cpp", "-o", tmp / "ack")
        print(run(tmp / "ack").strip())

        registration = register.split("void DeviceRegister::run()", 1)[1].split(
            "DeviceRegister::RegisterResult DeviceRegister::registerDevice()", 1)[0]
        (tmp / "registration-production.inc").write_text("void DeviceRegister::run()" + registration)
        run(*compiler, HOST / "registration.cpp", "-o", tmp / "registration")
        print(run(tmp / "registration").strip())

        main_source = (ROOT / "src/main.cpp").read_text()
        tracker = main_source.split("class ConnectionTracker", 1)[1].split("struct ControlTaskContext", 1)[0]
        network = main_source.split("void syncTask(", 1)[1].split("void controlTask(", 1)[0]
        (tmp / "network-production.inc").write_text("class ConnectionTracker" + tracker + "void syncTask(" + network)
        run(*compiler, HOST / "network.cpp", "-o", tmp / "network")
        print(run(tmp / "network").strip())


if __name__ == "__main__":
    main()
