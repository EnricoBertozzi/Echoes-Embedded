#pragma once

#include <cstddef>
#include <cstdint>
class FileStorage {
public:
  virtual ~FileStorage() = default;

  virtual bool save(char *filename, uint8_t *data, size_t size) = 0;
  virtual bool exists(char *filename) = 0;
  virtual bool remove(char *filename) = 0;
};
