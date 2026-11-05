#include "cage/firewall.hpp"
#include "control.hpp"

#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int integer(const std::string& value) {
    int number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument("Expected a decimal PID");
    return number;
}
bool enabled(const std::string& value) {
    if (value == "on") return true;
    if (value == "off") return false;
    throw std::invalid_argument("Use on or off");
}
}

int main(int argc, char** argv) {
    using namespace cage::firewall;
    try {
        if (argc < 3) {
            std::cerr << "Usage: cage-firewall setup PID PROXY_IPV4 on|off\n"
                         "       cage-firewall set PID on|off\n"
                         "       cage-firewall status PID\n"
                         "       cage-firewall kill CONTAINER_NAME_OR_ID\n"
                         "       cage-firewall stop PID\n"
                         "Run on the Linux Docker host, outside the workload.\n";
            return 2;
        }
        const std::string action = argv[1];
        if (action == "kill" && argc == 3) {
            control::kill_managed_container(argv[2]);
            std::cout << "Sandbox terminated; all its processes and network are stopped.\n";
            return 0;
        }
        if ((action == "setup" && argc != 5) || (action == "set" && argc != 4) ||
            ((action == "stop" || action == "status") && argc != 3) ||
            (action != "setup" && action != "set" && action != "stop" && action != "status"))
            throw std::invalid_argument("Invalid firewall command or argument count");
        const auto pid = integer(argv[2]);
        Guard guard(pid);
        if (action == "status") {
            control::File lock(control::lock(pid));
            const auto policy = control::load(pid, guard);
            std::cout << "Internet: " << (policy.internet ? "on" : "off") << '\n' << guard.counters();
        } else if (action == "stop") {
            guard.emergency_stop();
            std::cout << "Sandbox namespace terminated.\n";
        } else {
            try {
                control::File lock(control::lock(pid));
                auto policy = action == "setup" ? Policy{enabled(argv[4]), argv[3], 3128} : control::load(pid, guard);
                if (policy.proxy_ipv4.empty()) throw std::invalid_argument("A proxy IPv4 address is required");
                if (action == "set") policy.internet = enabled(argv[3]);
                guard.apply(policy);
                control::save(pid, guard, policy);
                std::cout << "Internet " << (policy.internet ? "enabled through isolated proxy" : "disabled") << ".\n";
            } catch (...) {
                // A missing/stale policy or failed state commit must not leave
                // the operator believing a running workload is protected.
                guard.emergency_stop();
                throw;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Firewall error: " << error.what() << '\n';
        return 1;
    }
}
