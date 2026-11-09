#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace JwtBuilder {

std::string base64Url(const uint8_t *data, size_t len);
std::string base64Url(const std::string &data);

std::string derToJose(const std::string &der);

} // namespace JwtBuilder
