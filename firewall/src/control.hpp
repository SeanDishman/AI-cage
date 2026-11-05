#pragma once

#include "cage/firewall.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cage::firewall::control {

class File {
public:
    explicit File(int value = -1) : value_(value) {}
    ~File() { if (value_ >= 0) close(value_); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    int get() const { return value_; }
private:
    int value_;
};

inline int open_state_directory() {
    constexpr const char* path = "/run/aicage-firewall";
    if (geteuid() != 0) throw std::runtime_error("Firewall control requires root on the Docker host");
    if (mkdir(path, 0700) != 0 && errno != EEXIST)
        throw std::runtime_error("Cannot create firewall state directory");
    const auto fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("Cannot open firewall state directory");
    struct stat status{};
    if (fstat(fd, &status) != 0 || !S_ISDIR(status.st_mode) || status.st_uid != 0 || (status.st_mode & 0077)) {
        close(fd);
        throw std::runtime_error("Firewall state directory must be owned by root with mode 0700");
    }
    return fd;
}

inline int lock(int pid) {
    File directory(open_state_directory());
    const auto name = std::to_string(pid) + ".lock";
    const auto fd = openat(directory.get(), name.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) throw std::runtime_error("Cannot open firewall policy lock");
    struct stat status{};
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_uid != 0 || (status.st_mode & 0077)) {
        close(fd);
        throw std::runtime_error("Unsafe firewall policy lock");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if ((errno != EAGAIN && errno != EINTR) || std::chrono::steady_clock::now() >= deadline) {
            close(fd);
            throw std::runtime_error("Cannot acquire firewall policy lock");
        }
        poll(nullptr, 0, 20);
    }
    return fd;
}

inline void save(int pid, const Guard& guard, const Policy& policy) {
    File directory(open_state_directory());
    const auto filename = std::to_string(pid);
    const auto temporary = filename + "." + std::to_string(getpid()) + ".tmp";
    const auto payload = guard.namespace_identity() + "\n" + policy.proxy_ipv4 + "\n" +
                         std::to_string(policy.proxy_port) + "\n" + (policy.internet ? "on\n" : "off\n");
    File output(openat(directory.get(), temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (output.get() < 0) throw std::runtime_error("Cannot create firewall state file");
    std::size_t offset = 0;
    while (offset < payload.size()) {
        const auto size = write(output.get(), payload.data() + offset, payload.size() - offset);
        if (size < 0 && errno == EINTR) continue;
        if (size <= 0) {
            unlinkat(directory.get(), temporary.c_str(), 0);
            throw std::runtime_error("Cannot write firewall state file");
        }
        offset += static_cast<std::size_t>(size);
    }
    if (fsync(output.get()) != 0 || renameat(directory.get(), temporary.c_str(), directory.get(), filename.c_str()) != 0) {
        unlinkat(directory.get(), temporary.c_str(), 0);
        throw std::runtime_error("Cannot commit firewall state");
    }
}

inline Policy load(int pid, const Guard& guard) {
    File directory(open_state_directory());
    File input(openat(directory.get(), std::to_string(pid).c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (input.get() < 0) throw std::runtime_error("No firewall policy exists for this sandbox; use setup first");
    struct stat status{};
    if (fstat(input.get(), &status) != 0 || !S_ISREG(status.st_mode) || status.st_uid != 0 ||
        (status.st_mode & 0077) || status.st_size > 4096)
        throw std::runtime_error("Unsafe firewall policy file");
    std::array<char, 4097> bytes{};
    std::size_t count = 0;
    while (count < bytes.size() - 1) {
        const auto size = read(input.get(), bytes.data() + count, bytes.size() - count - 1);
        if (size < 0 && errno == EINTR) continue;
        if (size < 0) throw std::runtime_error("Cannot read firewall policy");
        if (size == 0) break;
        count += static_cast<std::size_t>(size);
    }
    std::istringstream stream(std::string(bytes.data(), count));
    std::string identity, address, mode, trailing;
    unsigned port = 0;
    if (!(stream >> identity >> address >> port >> mode) || (stream >> trailing) || port == 0 || port > 65535 ||
        (mode != "on" && mode != "off")) throw std::runtime_error("Invalid firewall policy file");
    if (identity != guard.namespace_identity()) throw std::runtime_error("Stale firewall policy: sandbox identity changed");
    Policy policy{mode == "on", address, static_cast<std::uint16_t>(port)};
    (void)ipv4_rules(policy);
    return policy;
}

inline std::string docker(const std::vector<std::string>& parameters) {
    const char* executable = nullptr;
    for (const auto* candidate : {"/usr/local/bin/docker", "/usr/bin/docker", "/bin/docker"})
        if (access(candidate, X_OK) == 0) { executable = candidate; break; }
    if (!executable) throw std::runtime_error("Docker CLI is not installed on this Linux host");
    std::vector<std::string> arguments{executable, "--host", "unix:///var/run/docker.sock",
                                     "--config", "/run/aicage-firewall/docker-client"};
    arguments.insert(arguments.end(), parameters.begin(), parameters.end());
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    int pipes[2];
    if (pipe2(pipes, O_CLOEXEC) != 0) throw std::runtime_error("Cannot capture Docker response");
    File input(pipes[0]), output(pipes[1]);
    File null_input(open("/dev/null", O_RDONLY | O_CLOEXEC));
    char path[] = "PATH=/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
    char locale[] = "LC_ALL=C";
    char* environment[]{path, locale, nullptr};
    const auto child = fork();
    if (child < 0) throw std::runtime_error("Cannot start Docker CLI");
    if (child == 0) {
        if (dup2(output.get(), STDOUT_FILENO) < 0 || dup2(output.get(), STDERR_FILENO) < 0 ||
            dup2(null_input.get(), STDIN_FILENO) < 0) _exit(125);
        execve(executable, argv.data(), environment);
        _exit(127);
    }
    fcntl(input.get(), F_SETFL, O_NONBLOCK);
    std::string result;
    std::array<char, 4096> buffer{};
    auto drain = [&] {
        for (;;) {
            const auto length = read(input.get(), buffer.data(), buffer.size());
            if (length <= 0) break;
            if (result.size() < 65536) result.append(buffer.data(), static_cast<std::size_t>(length));
        }
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    int status = 0;
    for (;;) {
        drain();
        const auto waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if (waited < 0 && errno != EINTR) throw std::runtime_error("Cannot wait for Docker CLI");
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            throw std::runtime_error("Docker control command timed out");
        }
        pollfd ready{input.get(), POLLIN, 0};
        poll(&ready, 1, 25);
    }
    drain();
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("Docker control failed: " + result);
    return result;
}

inline void kill_managed_container(const std::string& name) {
    if (name.empty() || name.size() > 128 || name.front() == '-' ||
        !std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        })) throw std::invalid_argument("Invalid container name or ID");
    const auto details = docker({"container", "inspect", "--format",
        "{{.Id}}\n{{index .Config.Labels \"io.aicage.managed\"}}\n{{.State.Running}}\n{{.HostConfig.RestartPolicy.Name}}", name});
    std::istringstream stream(details);
    std::string id, managed, running, restart;
    std::getline(stream, id); std::getline(stream, managed); std::getline(stream, running); std::getline(stream, restart);
    if (managed != "true" || id.size() != 64 ||
        !std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        throw std::runtime_error("Refusing to stop a container without io.aicage.managed=true");
    // Always act on the immutable ID returned by inspect, never the name.
    std::string restart_error;
    if (restart != "no" && !restart.empty()) {
        try { (void)docker({"container", "update", "--restart=no", id}); }
        catch (const std::exception& error) { restart_error = error.what(); }
    }
    if (running == "true") (void)docker({"container", "kill", "--signal", "KILL", id});
    const auto state = docker({"container", "inspect", "--format", "{{.State.Running}}", id});
    if (state != "false\n") throw std::runtime_error("Container shutdown was not confirmed");
    if (!restart_error.empty()) throw std::runtime_error("Container stopped but restart policy update failed: " + restart_error);
}

} // namespace cage::firewall::control
