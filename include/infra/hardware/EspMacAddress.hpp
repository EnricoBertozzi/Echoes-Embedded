#pragma once

#include <string>

// Hardware identity: the eFuse MAC burned into the chip, formatted as the
// platform contract expects (uppercase with colons, AA:BB:CC:DD:EE:FF).
// Unlike the Wi-Fi interface MAC it never changes and does not depend on the
// Wi-Fi driver being started; it also matches the MAC printed on the board.
namespace EspMacAddress {
std::string get();
}
