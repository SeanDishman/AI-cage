#include "cage/firewall.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;
void check(bool passed, const std::string& label) {
    if (!passed) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
}
}

int main() {
    using namespace cage::firewall;
    for (const auto* address : {"1.1.1.1", "8.8.8.8", "93.184.216.34", "2606:4700:4700::1111"})
        check(is_public_address(address), std::string("Public: ") + address);
    for (const auto* address : {"", "localhost", "127.0.0.1", "127.0.0.11", "0.0.0.0", "10.0.0.1",
        "172.16.0.1", "172.31.255.255", "192.168.1.1", "100.64.0.1", "100.127.255.255",
        "169.254.169.254", "192.0.0.8", "192.0.2.1", "198.18.0.1", "198.51.100.1", "203.0.113.1",
        "224.0.0.1", "255.255.255.255", "2130706433", "0177.0.0.1", "0x7f000001", "127.1",
        "1.1.1.1\n-A OUTPUT -j ACCEPT", "::1", "::", "::ffff:127.0.0.1", "::ffff:8.8.8.8",
        "fe80::1", "fc00::1", "ff02::1", "64:ff9b::7f00:1", "2002:7f00:1::", "2001:db8::1",
        "2001::1", "3fff::1", "fe80::1%eth0"})
        check(!is_public_address(address), std::string("Denied: ") + address);
    check(!is_public_address(std::string("8.8.8.8\0ignored", 15)), "Embedded NUL is rejected");
    const auto offline = ipv4_rules(Policy{});
    check(offline.find(":OUTPUT DROP") != std::string::npos, "Offline defaults deny");
    check(offline.find("ESTABLISHED") == std::string::npos, "Offline has no existing-session bypass");
    check(offline.find("127.0.0.11") == std::string::npos, "No Docker DNS exception");
    Policy proxy{true, "172.29.0.2", 3128};
    const auto restricted = ipv4_rules(proxy);
    check(restricted.find("-d 172.29.0.2/32 -p tcp --dport 3128") != std::string::npos, "Proxy allow rule");
    check(restricted.find("--dport 53") == std::string::npos, "Proxy mode has no DNS egress");
    check(restricted.find("-A OUTPUT -p tcp") == std::string::npos, "Proxy cannot bypass by direct TCP");
    check(ipv6_rules().find("-A OUTPUT -o lo -s ::1/128 -d ::1/128 -j ACCEPT") != std::string::npos,
          "IPv6 loopback permitted");
    check(ipv6_rules().find("-A OUTPUT -j CAGE_DENIED") != std::string::npos, "External IPv6 blocked");
    for (const auto* bad : {"127.0.0.1\n-A OUTPUT -j ACCEPT", "1.2.3.4/33", "1.2.3.4/-1", "localhost",
                           "", "0.0.0.0", "127.0.0.1", "224.0.0.1", "255.255.255.255"}) {
        bool rejected = false;
        try { Policy p{true, bad, 3128}; (void)ipv4_rules(p); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Malformed policy is rejected");
    }
    std::cout << (failures ? "Firewall policy checks failed\n" : "Firewall policy checks passed\n");
    return failures ? 1 : 0;
}
