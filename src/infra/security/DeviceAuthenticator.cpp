#include "infra/security/DeviceAuthenticator.hpp"
#include "application/security/JwtBuilder.hpp"
#include <time.h>

namespace {
// 2020-01-01: below this the clock is not synced and `exp` would be invalid.
constexpr time_t MIN_VALID_TIME = 1577836800;
constexpr long TOKEN_TTL_SECONDS = 300;
} // namespace

DeviceAuthenticator::DeviceAuthenticator(DeviceKeys &keys, std::string mac)
    : keys(keys), mac(std::move(mac)) {}

std::string DeviceAuthenticator::issueToken() {
  if (!keys.isLoaded()) {
    return {};
  }

  const time_t now = time(nullptr);
  if (now < MIN_VALID_TIME) {
    return {};
  }

  // The MAC is hexadecimal with colons, no JSON escaping needed.
  const std::string payloadJson = "{\"iss\":\"echoes-device\",\"mac\":\"" +
                                  mac + "\",\"exp\":" +
                                  std::to_string(now + TOKEN_TTL_SECONDS) +
                                  "}";

  const std::string signingInput =
      JwtBuilder::base64Url(R"({"alg":"ES256","typ":"JWT"})") + "." +
      JwtBuilder::base64Url(payloadJson);

  std::string der;
  if (!keys.sign(signingInput, der)) {
    return {};
  }

  // ES256 JWTs carry the signature as raw R || S (JOSE), not DER.
  const std::string jose = JwtBuilder::derToJose(der);
  if (jose.size() != 64) {
    return {};
  }

  return signingInput + "." + JwtBuilder::base64Url(jose);
}
