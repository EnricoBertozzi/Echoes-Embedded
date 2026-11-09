#include "application/security/JwtBuilder.hpp"
#include <cstring>

namespace JwtBuilder {
namespace {

const char kTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

} // namespace

std::string base64Url(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve((len / 3 + 1) * 4);
  size_t i = 0;
  while (i + 3 <= len) {
    const uint32_t v =
        (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
    out += kTable[(v >> 18) & 0x3F];
    out += kTable[(v >> 12) & 0x3F];
    out += kTable[(v >> 6) & 0x3F];
    out += kTable[v & 0x3F];
    i += 3;
  }
  if (len - i == 1) {
    const uint32_t v = uint32_t(data[i]) << 16;
    out += kTable[(v >> 18) & 0x3F];
    out += kTable[(v >> 12) & 0x3F];
  } else if (len - i == 2) {
    const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
    out += kTable[(v >> 18) & 0x3F];
    out += kTable[(v >> 12) & 0x3F];
    out += kTable[(v >> 6) & 0x3F];
  }
  return out;
}

std::string base64Url(const std::string &data) {
  return base64Url(reinterpret_cast<const uint8_t *>(data.data()), data.size());
}

std::string derToJose(const std::string &der) {
  // Ecdsa-Sig-Value ::= SEQUENCE { r INTEGER, s INTEGER }
  const uint8_t *p = reinterpret_cast<const uint8_t *>(der.data());
  const size_t len = der.size();
  if (len < 8 || len > 72 || p[0] != 0x30) {
    return {};
  }
  if (p[1] != len - 2) { // P-256 signatures always use short-form lengths
    return {};
  }

  size_t pos = 2;
  uint8_t out[64] = {0};
  for (int part = 0; part < 2; part++) {
    if (pos + 2 > len || p[pos] != 0x02) {
      return {};
    }
    const size_t intLen = p[pos + 1];
    pos += 2;
    if (intLen == 0 || intLen > 33 || pos + intLen > len) {
      return {};
    }

    const uint8_t *src = p + pos;
    size_t n = intLen;
    if (n == 33) { // leading zero byte that keeps the INTEGER positive
      if (src[0] != 0x00) {
        return {};
      }
      src++;
      n--;
    }
    while (n > 0 && src[0] == 0x00) { // DER strips insignificant zeros
      src++;
      n--;
    }
    if (n > 32) {
      return {};
    }
    std::memcpy(out + 32 * part + (32 - n), src, n);
    pos += intLen;
  }

  return std::string(reinterpret_cast<const char *>(out), 64);
}

} // namespace JwtBuilder
