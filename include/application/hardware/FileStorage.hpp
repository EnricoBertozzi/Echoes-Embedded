#pragma once

#include <cstddef>
#include <cstdint>
class FileStorage {
public:
  virtual ~FileStorage() = default;

  virtual bool save(char *filename, uint8_t *data, size_t size);
  virtual bool exists(char *filename);
  virtual bool remove(char *filename);
};
