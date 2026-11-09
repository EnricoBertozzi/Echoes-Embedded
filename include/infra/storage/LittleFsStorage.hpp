#pragma once

#include "application/hardware/FileStorage.hpp"

class LittleFsStorage : public FileStorage {
public:
  bool save(const char *filename, const uint8_t *data, size_t size) override;
  bool load(const char *filename, std::string &out) override;
  bool exists(const char *filename) override;
  bool remove(const char *filename) override;
};
