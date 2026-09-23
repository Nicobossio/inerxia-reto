#include "domain/ip_address.hpp"

#include <string>

#include "domain/domain_error.hpp"

namespace inerxia::domain {
namespace {

bool is_valid_ipv4(const std::string& value) {
    int octets = 0;
    std::size_t pos = 0;
    while (pos < value.size()) {
        const std::size_t end = value.find('.', pos);
        const std::size_t token_end = (end == std::string::npos) ? value.size() : end;
        const std::string token = value.substr(pos, token_end - pos);

        if (token.empty() || token.size() > 3) {
            return false;
        }
        for (const char ch : token) {
            if (ch < '0' || ch > '9') {
                return false;
            }
        }

        int octet = 0;
        for (const char ch : token) {
            octet = octet * 10 + (ch - '0');
        }
        if (octet > 255) {
            return false;
        }

        ++octets;
        if (end == std::string::npos) {
            break;
        }
        pos = end + 1;
    }
    return octets == 4;
}

}  // namespace

IPAddress::IPAddress(std::string value) : value_(std::move(value)) {
    if (!is_valid_ipv4(value_)) {
        throw DomainError("Invalid IPv4 address: " + value_);
    }
}

}  // namespace inerxia::domain