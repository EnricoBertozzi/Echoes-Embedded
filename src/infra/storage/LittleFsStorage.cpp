#include "infra/storage/LittleFsStorage.hpp"
#include <LittleFS.h>

bool LittleFsStorage::save(const char *filename, const uint8_t *data,
                           size_t size) {
  File file = LittleFS.open(filename, FILE_WRITE);
  if (!file) {
    return false;
  }
  const size_t written = file.write(data, size);
  file.close();
  return written == size;
}

bool LittleFsStorage::load(const char *filename, std::string &out) {
  out.clear();
  if (!LittleFS.exists(filename)) {
    return false;
  }
  File file = LittleFS.open(filename, FILE_READ);
  if (!file) {
    return false;
  }
  out.resize(file.size());
  const size_t read = out.empty()
                          ? 0
                          : file.read(reinterpret_cast<uint8_t *>(&out[0]),
                                      out.size());
  file.close();
  if (read != out.size()) {
    out.clear();
    return false;
  }
  return true;
}

bool LittleFsStorage::exists(const char *filename) {
  return LittleFS.exists(filename);
}

bool LittleFsStorage::remove(const char *filename) {
  return LittleFS.remove(filename);
}
