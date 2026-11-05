// Native, static test fixture. Runs only inside disposable test containers.
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int main(int argc, char** argv) {
    signal(SIGCHLD, SIG_IGN);
    if (argc == 2 && std::strcmp(argv[1], "echo") == 0) {
        pollfd listeners[2]{};
        for (int i = 0; i < 2; ++i) {
            const int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) return 1;
            int reuse = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_ANY);
            local.sin_port = htons(static_cast<unsigned short>(3128 + i));
            if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) || listen(fd, 16)) return 2;
            listeners[i] = pollfd{fd, POLLIN, 0};
        }
        for (;;) {
            if (poll(listeners, 2, -1) < 0 && errno != EINTR) return 3;
            for (auto& listener : listeners) {
                if (!(listener.revents & POLLIN)) continue;
                const auto client = accept(listener.fd, nullptr, nullptr);
                if (client < 0) continue;
                if (fork() == 0) {
                    for (auto& other : listeners) close(other.fd);
                    char buffer[4096];
                    for (;;) {
                        const auto count = read(client, buffer, sizeof(buffer));
                        if (count <= 0) break;
                        ssize_t done = 0;
                        while (done < count) {
                            const auto written = write(client, buffer + done, static_cast<size_t>(count - done));
                            if (written <= 0) _exit(0);
                            done += written;
                        }
                    }
                    _exit(0);
                }
                close(client);
            }
        }
    }
    // A real process tree for the emergency-stop check.
    if (fork() == 0) {
        if (fork() == 0) for (;;) pause();
        for (;;) pause();
    }
    for (;;) pause();
}
