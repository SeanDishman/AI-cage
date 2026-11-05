# AICage

A C++20 sandbox controller for running Codex on Windows through an isolated Linux container in WSL2. Codex has root permissions **inside its container**: it can download packages, install tools, compile programs and execute commands. Its Windows launcher, network controls and access-attempt logs live outside that container.

## Use it

Build with `powershell -ExecutionPolicy Bypass -File scripts/build.ps1` if the binaries are not already present. This project uses the existing `ShannonDocker` WSL distribution; choose another with `scripts/build.ps1 -Distro YOUR_DISTRO`.

Double-click **`Open AICage.cmd`** for the Electron desktop chat app. Its native window is titled **AICage** for OBS Window Capture. See [desktop instructions](ui/README.md). **`Start Sandbox.cmd`** opens console controls, and **`Open Codex.cmd`** opens Codex directly. Use PowerShell for individual commands:

```powershell
.\build\AICage.exe setup          # first use: build images and create a private workspace
.\build\AICage.exe internet on    # allow public HTTP/HTTPS downloads and Codex requests
.\build\AICage.exe codex          # interactive Codex, full access inside the container
```

Open a second PowerShell window to operate the controls while Codex is running:

```powershell
.\build\AICage.exe internet off   # revoke network access, including existing connections
.\build\AICage.exe internet on
.\build\AICage.exe sensors off   # stop recording access attempts; isolation stays enforced
.\build\AICage.exe sensors on
.\build\AICage.exe logs
.\build\AICage.exe logs --proxy  # proxy destinations and HTTP rejection status
.\build\AICage.exe status
.\build\AICage.exe stop          # emergency stop of the workload and its children
```

**Cloud Codex requires internet access.** Switching it off also interrupts Codex's connection to OpenAI. Local programs can continue working offline.

Additional commands:

```powershell
.\build\AICage.exe shell
.\build\AICage.exe run python3 --version
.\build\AICage.exe run bash -lc 'apt-get update && apt-get install -y jq'
.\build\AICage.exe run bash -lc 'python3 -m venv /workspace/venv'
.\build\AICage.exe start
```

Files in `/workspace`, the container home directory and installed system packages survive `stop`/`start`. There is no automatic host folder sharing or file export. A separate instance can be created with `--name experiment` before a command. Each instance has separate networks, home and workspace volumes.

## Your Codex account

Codex supports copying its auth cache to a container as a headless-login fallback. Only `auth.json` is imported; host configuration, memories, plugins, SSH keys and other folders are not copied. [OpenAI authentication documentation](https://developers.openai.com/codex/auth/).

Import your existing login while the sandbox is stopped. Replace `YOUR_WINDOWS_USER` with your Windows profile folder name:

```powershell
.\build\AICage.exe stop
.\build\AICage.exe import-auth /mnt/c/Users/YOUR_WINDOWS_USER/.codex/auth.json
.\build\AICage.exe start
.\build\AICage.exe run codex login status
```

Alternatively, turn internet on, run `AICage.exe login`, and complete the device-code sign-in in your browser. Device-code login must be enabled for your account. Passwords and tokens should never be pasted into this project's source files or chat.

The imported credential is stored in the sandbox's `/root/.codex/auth.json`. Programs given root access **inside the sandbox can read that credential**. Public internet access allows sandbox data to be sent to public servers. This design isolates your computer; it does not make an account credential safe from code that you intentionally allow to read it. Use a separate login/account for hostile software if that distinction matters to your use.

## Isolation and monitoring

| Control | Enforcement |
|---|---|
| Host files | Private Docker mount namespace; no Windows folders or host home mounted |
| Host control | No Docker socket, host PID namespace, host networking, published ports or device/GPU passthrough |
| Program privileges | Container root with a small capability set; no `SYS_ADMIN`, `NET_ADMIN`, `NET_RAW` or `SYS_PTRACE`; no-new-privileges |
| System calls | Docker's default seccomp policy plus a second filter denying namespace creation, mounts, tracing, async I/O and untraced clones |
| Internet off | Firewall inside the workload's network namespace blocks external packets; only its own loopback remains |
| Internet on | Only the separate proxy at port 3128 is reachable; it permits public HTTP/HTTPS on ports 80/443 and rejects private/loopback/metadata destinations |
| Sensors | An external host `ptrace` monitor follows the broker and all workload descendants, recording denied calls, selected suspicious paths and network failures |
| Monitor failure | `PTRACE_O_EXITKILL` terminates traced processes; killing container PID 1 terminates its remaining processes |
| Resource limits | 4 GiB memory with no additional swap, two CPU cores, 256 processes; bounded log rotation |

The trusted broker is a static C++ executable mounted as **one read-only file** at `/aicage/broker`. This keeps the command-entry code immutable while allowing the rest of the workload filesystem to be writable. Startup is gated until the firewall and external monitor are active. Every `run`, `shell` and `codex` command is spawned by the monitored PID 1 broker. The small Docker-exec client only relays terminal data to that broker.

The controls and logs are under `/var/lib/aicage/<name>` on the WSL host and are not mounted into the workload. Log entries contain syscall names, path names, destinations and results; they do not collect file contents or command-line arguments. Proxy access logs omit URL paths and query strings. The sensor switch changes recording within about one second and does not remove the underlying protection or monitor fail-stop behavior.

Example sensor event:

```json
{"time":1793908800,"type":"access_denied","detail":"pid=123 mount  result=-1"}
```

An event shows an observed operation, **not proof of malicious intent or a successful escape**. Normal programs can probe unavailable paths. The sensors cover selected filesystem calls, network failures and privileged operations; they cannot detect every possible breakout technique. The default workload supports x86-64 executables. Unsupported syscall architectures and `io_uring` are blocked to avoid known gaps in this monitor's coverage. Tracing adds overhead to workloads with many system calls.

Use the controller to launch workloads. An administrator manually invoking arbitrary `docker exec` commands bypasses the monitored process ancestry, although the container and network restrictions still apply.

## Security limits

This is a real container boundary inside WSL2, **not a guarantee that arbitrary hostile code can never escape**. It relies on the installed Windows/WSL kernel, Docker, seccomp and Squid. It has not received an independent security audit. Keep those components updated; use a separate disposable VM or machine for deliberately hostile code or research involving unknown exploits.

Containers share the WSL kernel with other WSL workloads. CPU/process/memory limits are enforced, but persistent container/volume disk usage currently has **no hard quota**. Do not use this version for uncontrolled disk-fill workloads. Network-on mode is a public-web proxy, not a destination allowlist; it does not isolate public services or accounts you deliberately connect to. The project does not enable a GPU, camera, microphone or host desktop access.

Docker's security filter is retained rather than replaced with `unconfined`. See [Docker seccomp](https://docs.docker.com/engine/security/seccomp/) and [bridge networking](https://docs.docker.com/engine/network/drivers/bridge/) for the underlying mechanisms.

## Build and verify

Requirements: Windows CMake and a C++ compiler; an x86-64 Linux WSL2 distribution with CMake, g++, Python 3, Docker, iptables/ip6tables and root access. No changes to global Docker daemon settings or host firewall policy are required. The base image is pinned by digest; Codex CLI is pinned to version `0.159.2`.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
wsl -d ShannonDocker -u root --exec ctest --test-dir '/mnt/c/Users/dontl/Downloads/C++ sandbox/build-linux' --output-on-failure
wsl -d ShannonDocker -u root --exec python3 '/mnt/c/Users/dontl/Downloads/C++ sandbox/tests/integration.py' main
wsl -d ShannonDocker -u root --exec python3 '/mnt/c/Users/dontl/Downloads/C++ sandbox/tests/terminal.py'
```

The integration suite uses harmless probes to verify allowed writes/compilation, host-file absence, mount and namespace denials, public web access, private-address rejection, sensor switches, exit codes, persistence and monitor-loss shutdown. Run it before importing credentials or in a separate test instance: it intentionally kills and restarts that instance, writes sample files, and leaves it stopped. Results are saved to `artifacts/integration-results.json`.

Non-interactive commands preserve stdin, stdout and stderr as separate byte streams. This supports `AICage.exe run codex exec --json --skip-git-repo-check -` with the prompt on stdin. Interactive commands receive a PTY with terminal resize support. See [VERIFICATION.md](VERIFICATION.md) for the checks actually completed on this workstation.
