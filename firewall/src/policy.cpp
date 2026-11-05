#include "cage/firewall.hpp"

#include <array>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace cage::firewall {
namespace {

struct Network { std::uint32_t address; unsigned bits; const char* text; };

// Conservative special-use exclusions. Some individually globally reachable
// exceptions within these ranges are intentionally also denied.
constexpr std::array<Network, 16> private_networks{{
    {0x00000000, 8, "0.0.0.0/8"},
    {0x0a000000, 8, "10.0.0.0/8"},
    {0x64400000, 10, "100.64.0.0/10"},
    {0x7f000000, 8, "127.0.0.0/8"},
    {0xa9fe0000, 16, "169.254.0.0/16"},
    {0xac100000, 12, "172.16.0.0/12"},
    {0xc0000000, 24, "192.0.0.0/24"},
    {0xc0000200, 24, "192.0.2.0/24"},
    {0xc0586300, 24, "192.88.99.0/24"},
    {0xc0a80000, 16, "192.168.0.0/16"},
    {0xc6120000, 15, "198.18.0.0/15"},
    {0xc6336400, 24, "198.51.100.0/24"},
    {0xcb007100, 24, "203.0.113.0/24"},
    {0xe0000000, 4, "224.0.0.0/4"},
    {0xf0000000, 4, "240.0.0.0/4"},
    {0xffffffff, 32, "255.255.255.255/32"},
}};

bool parse_v4(const std::string& address, std::uint32_t& host) {
    if (address.empty() || address.find('\0') != std::string::npos) return false;
    in_addr parsed{};
    if (inet_pton(AF_INET, address.c_str(), &parsed) != 1) return false;
    host = ntohl(parsed.s_addr);
    return true;
}

bool contains(std::uint32_t address, const Network& network) {
    const auto mask = network.bits == 0 ? 0u : (0xffffffffu << (32 - network.bits));
    return (address & mask) == (network.address & mask);
}

} // namespace

bool is_public_ipv4(const std::string& address) {
    std::uint32_t value = 0;
    if (!parse_v4(address, value)) return false;
    for (const auto& network : private_networks) if (contains(value, network)) return false;
    return true;
}

bool is_public_ipv6(const std::string& address) {
    if (address.empty() || address.find('\0') != std::string::npos) return false;
    in6_addr parsed{};
    if (inet_pton(AF_INET6, address.c_str(), &parsed) != 1) return false;
    const auto* b = reinterpret_cast<const unsigned char*>(&parsed);
    // Accept native global unicast only; reject IPv4-mapped, NAT64, multicast,
    // loopback, link-local, ULA and tunnelling/transition addresses.
    if ((b[0] & 0xe0u) != 0x20u) return false; // 2000::/3
    if (b[0] == 0x20 && b[1] == 0x02) return false; // 6to4
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] < 2) return false; // IETF 2001::/23
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x0d && b[3] == 0xb8) return false;
    if (b[0] == 0x3f && (b[1] & 0xf0u) == 0xf0u) return false; // 3fff::/20 docs
    return true;
}

bool is_public_address(const std::string& address) {
    return is_public_ipv4(address) || is_public_ipv6(address);
}

std::string ipv4_rules(const Policy& policy) {
    std::uint32_t proxy = 0;
    if (policy.internet && policy.proxy_ipv4.empty())
        throw std::invalid_argument("Internet access requires the isolated proxy endpoint");
    if (!policy.proxy_ipv4.empty() && (!parse_v4(policy.proxy_ipv4, proxy) || policy.proxy_port == 0))
        throw std::invalid_argument("Proxy requires a literal IPv4 address and a nonzero port");
    if (!policy.proxy_ipv4.empty() && (proxy == 0 || (proxy >> 24) == 127 || (proxy >> 28) >= 14))
        throw std::invalid_argument("Proxy must be a unicast address on the isolated bridge");

    std::ostringstream out;
    out << "*filter\n:INPUT DROP [0:0]\n:FORWARD DROP [0:0]\n:OUTPUT DROP [0:0]\n"
           ":CAGE_DENIED - [0:0]\n";
    // Exact loopback only. Docker's 127.0.0.11 DNS forwarder must not become an
    // alternative egress channel when the network is disabled or proxy-only.
    out << "-A INPUT -i lo -s 127.0.0.1/32 -d 127.0.0.1/32 -j ACCEPT\n"
           "-A OUTPUT -o lo -s 127.0.0.1/32 -d 127.0.0.1/32 -j ACCEPT\n";
    if (policy.internet) {
        out << "-A OUTPUT -d " << policy.proxy_ipv4 << "/32 -p tcp --dport " << policy.proxy_port
            << " -m conntrack --ctstate NEW,ESTABLISHED -j ACCEPT\n";
        out << "-A INPUT -s " << policy.proxy_ipv4 << "/32 -p tcp --sport " << policy.proxy_port
            << " -m conntrack --ctstate ESTABLISHED -j ACCEPT\n";
    }
    // No unconditional ESTABLISHED exception in OUTPUT: off must cut existing
    // TCP sessions as well as prevent new ones. Counters include denied probes.
    out << "-A INPUT -j CAGE_DENIED\n-A OUTPUT -j CAGE_DENIED\n"
           "-A FORWARD -j CAGE_DENIED\n-A CAGE_DENIED -j DROP\nCOMMIT\n";
    return out.str();
}

std::string ipv6_rules() {
    // IPv6 outside the sandbox is deliberately unavailable in both modes.
    // Exact local loopback remains useful for servers running within the cage.
    return "*filter\n:INPUT DROP [0:0]\n:FORWARD DROP [0:0]\n:OUTPUT DROP [0:0]\n"
           ":CAGE_DENIED - [0:0]\n"
           "-A INPUT -i lo -s ::1/128 -d ::1/128 -j ACCEPT\n"
           "-A OUTPUT -o lo -s ::1/128 -d ::1/128 -j ACCEPT\n"
           "-A INPUT -j CAGE_DENIED\n-A OUTPUT -j CAGE_DENIED\n"
           "-A FORWARD -j CAGE_DENIED\n-A CAGE_DENIED -j DROP\nCOMMIT\n";
}

} // namespace cage::firewall
