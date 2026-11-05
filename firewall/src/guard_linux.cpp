#include "cage/firewall.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cage::firewall {
namespace {

class Fd {
public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) close(fd_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }
    int release() { return std::exchange(fd_, -1); }
private:
    int fd_;
};

std::runtime_error system_error(const std::string& operation) {
    return std::runtime_error(operation + ": " + std::strerror(errno));
}

bool same_namespace(int first, int second) {
    struct stat a{}, b{};
    if (fstat(first, &a) || fstat(second, &b)) throw system_error("Inspect namespace");
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

bool is_namespace_init(int pid) {
    std::ifstream status("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("NSpid:", 0) != 0) continue;
        std::istringstream values(line.substr(6));
        int entry = 0, last = 0, count = 0;
        while (values >> entry) { last = entry; ++count; }
        return count >= 2 && last == 1;
    }
    return false;
}

std::string executable_path(const std::string& name) {
    for (const auto* directory : {"/usr/sbin/", "/sbin/", "/usr/bin/", "/bin/"}) {
        const auto path = directory + name;
        if (access(path.c_str(), X_OK) == 0) return path;
    }
    throw std::runtime_error("Required firewall tool is missing: " + name);
}

} // namespace

Guard::Guard(int namespace_init_pid) : init_pid_(namespace_init_pid) {
    if (init_pid_ <= 1 || init_pid_ == getpid())
        throw std::invalid_argument("Refusing host/self PID; a separate namespace init is required");
    Fd process(static_cast<int>(syscall(SYS_pidfd_open, init_pid_, 0)));
    if (process.get() < 0) throw system_error("Open target pidfd");
    const auto base = "/proc/" + std::to_string(init_pid_) + "/ns/";
    Fd target_net(open((base + "net").c_str(), O_RDONLY | O_CLOEXEC));
    Fd target_pid(open((base + "pid").c_str(), O_RDONLY | O_CLOEXEC));
    Fd own_net(open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC));
    Fd own_pid(open("/proc/self/ns/pid", O_RDONLY | O_CLOEXEC));
    if (target_net.get() < 0 || target_pid.get() < 0 || own_net.get() < 0 || own_pid.get() < 0)
        throw system_error("Open target namespaces");
    if (same_namespace(target_net.get(), own_net.get()) || same_namespace(target_pid.get(), own_pid.get()))
        throw std::invalid_argument("Refusing to modify the host network/PID namespace");
    if (!is_namespace_init(init_pid_))
        throw std::invalid_argument("Target must be PID 1 inside its own PID namespace");
    if (syscall(SYS_pidfd_send_signal, process.get(), 0, nullptr, 0) != 0)
        throw system_error("Target exited during namespace validation");
    pidfd_ = process.release();
    netfd_ = target_net.release();
}

Guard::~Guard() {
    if (netfd_ >= 0) close(netfd_);
    if (pidfd_ >= 0) close(pidfd_);
}

bool Guard::alive() const {
    pollfd item{pidfd_, POLLIN, 0};
    const auto result = poll(&item, 1, 0);
    if (result < 0) throw system_error("Poll target pidfd");
    return result == 0;
}

std::string Guard::namespace_identity() const {
    struct stat namespace_stat{};
    if (fstat(netfd_, &namespace_stat) != 0) throw system_error("Identify network namespace");
    std::ifstream process("/proc/" + std::to_string(init_pid_) + "/stat");
    std::string line;
    std::getline(process, line);
    const auto command_end = line.rfind(')');
    if (command_end == std::string::npos) throw std::runtime_error("Cannot read target process identity");
    std::istringstream fields(line.substr(command_end + 1));
    std::string value;
    for (int field = 3; field <= 22; ++field)
        if (!(fields >> value)) throw std::runtime_error("Incomplete target process identity");
    if (!alive()) throw std::runtime_error("Target exited during identity check");
    return std::to_string(namespace_stat.st_dev) + ":" + std::to_string(namespace_stat.st_ino) + ":" + value;
}

void Guard::signal(int number) const {
    if (syscall(SYS_pidfd_send_signal, pidfd_, number, nullptr, 0) != 0 && errno != ESRCH)
        throw system_error("Signal sandbox init");
}

std::string Guard::in_namespace(const std::vector<std::string>& arguments, const std::string& input) const {
    if (arguments.empty()) throw std::invalid_argument("Empty firewall command");
    const auto executable = executable_path(arguments.front());
    Fd source(static_cast<int>(syscall(SYS_memfd_create, "cage-firewall-rules", MFD_CLOEXEC)));
    if (source.get() < 0) throw system_error("Create firewall rules buffer");
    std::size_t offset = 0;
    while (offset < input.size()) {
        const auto written = write(source.get(), input.data() + offset, input.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) throw system_error("Write firewall rules buffer");
        offset += static_cast<std::size_t>(written);
    }
    if (lseek(source.get(), 0, SEEK_SET) < 0) throw system_error("Rewind firewall rules");
    int pipes[2];
    if (pipe2(pipes, O_CLOEXEC) != 0) throw system_error("Create firewall output pipe");
    Fd reader(pipes[0]), writer(pipes[1]);
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& item : arguments) argv.push_back(const_cast<char*>(item.c_str()));
    argv.push_back(nullptr);
    char path[] = "PATH=/usr/sbin:/usr/bin:/sbin:/bin";
    char locale[] = "LC_ALL=C";
    char* environment[]{path, locale, nullptr};
    const auto child = fork();
    if (child < 0) throw system_error("Start firewall command");
    if (child == 0) {
        if (dup2(source.get(), STDIN_FILENO) < 0 || dup2(writer.get(), STDOUT_FILENO) < 0 ||
            dup2(writer.get(), STDERR_FILENO) < 0) _exit(125);
        if (setns(netfd_, CLONE_NEWNET) != 0) _exit(126);
        execve(executable.c_str(), argv.data(), environment);
        _exit(127);
    }
    close(writer.release());
    const auto flags = fcntl(reader.get(), F_GETFL, 0);
    if (flags >= 0) fcntl(reader.get(), F_SETFL, flags | O_NONBLOCK);
    std::string output;
    std::array<char, 4096> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    int status = 0;
    bool ended = false;
    while (!ended) {
        pollfd poller{reader.get(), POLLIN | POLLHUP, 0};
        poll(&poller, 1, 30);
        for (;;) {
            const auto count = read(reader.get(), buffer.data(), buffer.size());
            if (count <= 0) break;
            if (output.size() < 256 * 1024) output.append(buffer.data(), static_cast<std::size_t>(count));
        }
        const auto result = waitpid(child, &status, WNOHANG);
        if (result == child) ended = true;
        else if (result < 0 && errno != EINTR) {
            kill(child, SIGKILL);
            throw system_error("Wait for firewall command");
        }
        if (!ended && std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            throw std::runtime_error("Firewall command timed out");
        }
    }
    // Drain any output written immediately before child exit.
    for (;;) {
        const auto count = read(reader.get(), buffer.data(), buffer.size());
        if (count <= 0) break;
        if (output.size() < 256 * 1024) output.append(buffer.data(), static_cast<std::size_t>(count));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        const auto code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        throw std::runtime_error("Firewall command failed (" + std::to_string(code) + "): " + output);
    }
    return output;
}

void Guard::apply(const Policy& policy) {
    try {
        if (!alive()) throw std::runtime_error("Sandbox has already exited");
        const auto v4 = ipv4_rules(policy);
        in_namespace({"ip6tables-restore", "--wait", "5"}, ipv6_rules());
        in_namespace({"iptables-restore", "--wait", "5"}, v4);
    } catch (...) {
        // Retain the original error, but fail closed before returning it.
        signal(SIGKILL);
        throw;
    }
}

void Guard::emergency_stop() {
    signal(SIGKILL);
    // The process kill never depends on firewall tools being available. The
    // pinned netns remains valid even while its owning container exits.
    try { in_namespace({"ip6tables-restore", "--wait", "5"}, ipv6_rules()); } catch (...) {}
    try { in_namespace({"iptables-restore", "--wait", "5"}, ipv4_rules(Policy{})); } catch (...) {}
    pollfd process{pidfd_, POLLIN, 0};
    int result;
    do { result = poll(&process, 1, 5000); } while (result < 0 && errno == EINTR);
    if (result <= 0) throw std::runtime_error("Stop signal sent, but namespace shutdown was not confirmed");
}

std::string Guard::counters() const {
    return "IPv4 denied packets:\n" + in_namespace({"iptables", "--wait", "5", "-n", "-v", "-x", "-L", "CAGE_DENIED"}, {}) +
           "IPv6 denied packets:\n" + in_namespace({"ip6tables", "--wait", "5", "-n", "-v", "-x", "-L", "CAGE_DENIED"}, {});
}

} // namespace cage::firewall
