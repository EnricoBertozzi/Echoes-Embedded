#include "infra/console/FileConsole.hpp"
#include <Arduino.h>

namespace {
// Two minutes to paste the content after "restore <file>".
constexpr uint32_t kRestoreTimeoutMs = 120000;
} // namespace

FileConsole::FileConsole(FileStorage &storage) : storage(storage) {}

void FileConsole::poll() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (!lineBuffer.empty()) {
        handleLine(lineBuffer);
        lineBuffer.clear();
      }
    } else {
      lineBuffer.push_back(c);
    }
  }

  if (restoring && millis() - lastRestoreActivityMs > kRestoreTimeoutMs) {
    Serial.println("[Console] restore timed out, discarded");
    restoring = false;
    restoreBuffer.clear();
  }
}

void FileConsole::handleLine(const std::string &line) {
  if (restoring) {
    lastRestoreActivityMs = millis();
    if (line == ".") {
      finishRestore();
    } else {
      restoreBuffer += line;
      restoreBuffer += '\n';
    }
    return;
  }

  if (line == "help") {
    Serial.println("commands: dump <file> | restore <file>");
    return;
  }
  if (line.rfind("dump ", 0) == 0) {
    dump(line.substr(5));
    return;
  }
  if (line.rfind("restore ", 0) == 0) {
    beginRestore(line.substr(8));
    return;
  }
}

void FileConsole::dump(const std::string &filename) {
  std::string content;
  if (!storage.load(filename.c_str(), content)) {
    Serial.printf("[Console] %s not found\n", filename.c_str());
    return;
  }
  Serial.println("-----BEGIN DUMP-----");
  Serial.print(content.c_str());
  if (content.empty() || content.back() != '\n') {
    Serial.println();
  }
  Serial.println("-----END DUMP-----");
}

void FileConsole::beginRestore(const std::string &filename) {
  restoring = true;
  restoreTarget = filename;
  restoreBuffer.clear();
  lastRestoreActivityMs = millis();
  Serial.printf("[Console] paste the content of %s now, finish with a line "
                "containing only \".\"\n",
                filename.c_str());
}

void FileConsole::finishRestore() {
  restoring = false;
  const bool ok = storage.save(
      restoreTarget.c_str(),
      reinterpret_cast<const uint8_t *>(restoreBuffer.data()),
      restoreBuffer.size());
  if (ok) {
    Serial.printf("[Console] %s restored (%u bytes); reboot to use it\n",
                  restoreTarget.c_str(), (unsigned)restoreBuffer.size());
  } else {
    Serial.printf("[Console] failed to write %s\n", restoreTarget.c_str());
  }
  restoreBuffer.clear();
}
