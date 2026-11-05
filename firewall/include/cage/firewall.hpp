#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cage::firewall {

// A numeric address only: no DNS resolution is performed by these helpers.
bool is_public_ipv4(const std::string& address);
bool is_public_ipv6(const std::string& address);
bool is_public_address(const std::string& address);

struct Policy {
    bool internet = false;
    // Required when enabled. Only this TCP proxy is reachable. The proxy must
    // independently reject non-public destinations and restrict allowed ports.
    std::string proxy_ipv4;
    std::uint16_t proxy_port = 3128;
};

// Render complete filter tables for a dedicated network namespace. No shell
// interpolation is used. Invalid addresses and ports are rejected.
std::string ipv4_rules(const Policy& policy);
std::string ipv6_rules();

#ifdef __linux__
// Only accepts the init process (namespace PID 1) of a separate PID AND network
// namespace. Uses a pidfd to avoid accidentally signalling a reused host PID.
// Mutations happen only in the target network namespace, never the host's.
class Guard {
public:
    explicit Guard(int namespace_init_pid);
    ~Guard();
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    Guard(Guard&&) = delete;
    Guard& operator=(Guard&&) = delete;

    // Applies IPv6-deny first, then IPv4 policy. On any enforcement failure,
    // terminates the namespace init and all its children (fail closed).
    void apply(const Policy& policy);
    // Kills the namespace init immediately, then best-effort closes its pinned
    // network namespace. Does not wait for a graceful program shutdown.
    void emergency_stop();
    bool alive() const;
    std::string counters() const;
    std::string namespace_identity() const;

private:
    int pidfd_ = -1;
    int netfd_ = -1;
    int init_pid_ = -1;
    void signal(int number) const;
    std::string in_namespace(const std::vector<std::string>& arguments,
                             const std::string& input) const;
};
#endif

} // namespace cage::firewall
