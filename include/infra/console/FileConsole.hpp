#pragma once

#include "application/hardware/FileStorage.hpp"
#include <cstdint>
#include <string>

class FileConsole {
public:
  explicit FileConsole(FileStorage &storage);

  void poll();

private:
  void handleLine(const std::string &line);
  void dump(const std::string &filename);
  void beginRestore(const std::string &filename);
  void finishRestore();

  FileStorage &storage;
  std::string lineBuffer;
  bool restoring{false};
  std::string restoreTarget;
  std::string restoreBuffer;
  uint32_t lastRestoreActivityMs{0};
};
