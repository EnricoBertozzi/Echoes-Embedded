#pragma once

#include "application/hardware/FileStorage.hpp"
#include "application/security/DeviceKeys.hpp"
#include <atomic>
#include <mbedtls/pk.h>
#include <mutex>
#include <string>

class MbedtlsDeviceKeys : public DeviceKeys {
public:
  explicit MbedtlsDeviceKeys(FileStorage &storage,
                             std::string path = "/private.pem");
  ~MbedtlsDeviceKeys() override;

  bool load() override;
  bool create() override;
  bool store() override;
  void reset() override;
  bool isLoaded() const override { return loaded; }
  std::string publicKeyPem() override;
  bool sign(const std::string &message, std::string &der) override;

private:
  // Hardware RNG callback for mbedtls.
  static int rng(void *ctx, unsigned char *buf, size_t len);

  FileStorage &storage;
  const std::string path;
  mbedtls_pk_context pk;
  std::atomic<bool> loaded{false};
  mutable std::mutex mutex; // the pk context is not thread-safe
};
