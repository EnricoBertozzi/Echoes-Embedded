#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class FileStorage {
public:
  virtual ~FileStorage() = default;

  virtual bool save(const char *filename, const uint8_t *data, size_t size) = 0;
  virtual bool load(const char *filename, std::string &out) = 0;
  virtual bool exists(const char *filename) = 0;
  virtual bool remove(const char *filename) = 0;
};
