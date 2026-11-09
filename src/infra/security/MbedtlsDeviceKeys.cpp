#include "infra/security/MbedtlsDeviceKeys.hpp"
#include <cstring>
#include <esp_system.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>

namespace {
constexpr size_t kPemBufferSize = 1600;
} // namespace

int MbedtlsDeviceKeys::rng(void * /*ctx*/, unsigned char *buf, size_t len) {
  esp_fill_random(buf, len);
  return 0;
}

MbedtlsDeviceKeys::MbedtlsDeviceKeys(FileStorage &storage, std::string path)
    : storage(storage), path(std::move(path)) {
  mbedtls_pk_init(&pk);
}

MbedtlsDeviceKeys::~MbedtlsDeviceKeys() { mbedtls_pk_free(&pk); }

bool MbedtlsDeviceKeys::load() {
  std::string pem;
  if (!storage.load(path.c_str(), pem)) {
    return false;
  }

  // mbedtls_pk_parse_key needs a null-terminated buffer.
  pem.push_back('\0');
  {
    std::lock_guard<std::mutex> lock(mutex);
    mbedtls_pk_free(&pk);
    mbedtls_pk_init(&pk);
    const int ret =
        mbedtls_pk_parse_key(&pk, reinterpret_cast<const unsigned char *>(
                                      pem.data()),
                              pem.size(), nullptr, 0);
    if (ret != 0) {
      return false;
    }
    loaded = true;
  }
  return true;
}

bool MbedtlsDeviceKeys::create() {
  std::lock_guard<std::mutex> lock(mutex);
  mbedtls_pk_free(&pk);
  mbedtls_pk_init(&pk);

  int ret = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  if (ret != 0) {
    return false;
  }
  ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk), rng,
                            nullptr);
  if (ret != 0) {
    return false;
  }

  loaded = true;
  return true;
}

bool MbedtlsDeviceKeys::store() {
  std::lock_guard<std::mutex> lock(mutex);
  unsigned char pem[kPemBufferSize];
  const int ret = mbedtls_pk_write_key_pem(&pk, pem, sizeof(pem));
  if (ret != 0) {
    return false;
  }
  if (!storage.save(path.c_str(), pem, strlen(reinterpret_cast<char *>(pem)))) {
    return false;
  }
  return true;
}

void MbedtlsDeviceKeys::reset() {
  std::lock_guard<std::mutex> lock(mutex);
  mbedtls_pk_free(&pk);
  mbedtls_pk_init(&pk);
  loaded = false;
}

std::string MbedtlsDeviceKeys::publicKeyPem() {
  std::lock_guard<std::mutex> lock(mutex);
  if (!loaded) {
    return {};
  }
  unsigned char pem[kPemBufferSize];
  const int ret = mbedtls_pk_write_pubkey_pem(&pk, pem, sizeof(pem));
  if (ret != 0) {
    return {};
  }
  return std::string(reinterpret_cast<char *>(pem));
}

bool MbedtlsDeviceKeys::sign(const std::string &message, std::string &der) {
  der.clear();
  std::lock_guard<std::mutex> lock(mutex);
  if (!loaded) {
    return false;
  }

  // In this mbedtls the int-returning variants carry the _ret suffix.
  unsigned char hash[32];
  if (mbedtls_sha256_ret(
          reinterpret_cast<const unsigned char *>(message.data()),
          message.size(), hash, 0) != 0) { // 0 = SHA-256 (not SHA-224)
    return false;
  }

  unsigned char
      sig[MBEDTLS_PK_SIGNATURE_MAX_SIZE]; // > MBEDTLS_ECDSA_MAX_LEN
  size_t sigLen = 0;
  const int ret = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                                  sig, &sigLen, rng, nullptr);
  if (ret != 0) {
    return false;
  }

  der.assign(reinterpret_cast<const char *>(sig), sigLen);
  return true;
}
