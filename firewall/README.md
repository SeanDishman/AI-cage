# Sandbox firewall

This C++20 component enforces the network boundary on the Linux Docker host,
outside the AI's container. It changes only the **workload's network namespace**.
It does not edit Windows Firewall, the Linux host's rules, or Docker's host chains.

## Controls

```text
cage-firewall setup <host-init-pid> <proxy-ipv4> on|off
cage-firewall set <host-init-pid> on|off
cage-firewall status <host-init-pid>
cage-firewall kill <managed-container-name-or-id>
```

`setup` installs the firewall while the workload's init process is still gated.
The AI must not run until setup succeeds. The proxy endpoint is TCP port 3128.
`set off` cuts both new and already established external connections. Local
programs can keep running; localhost services still work. `kill` is the emergency
stop: it terminates the container and its child processes, verifies shutdown,
and disables an automatic restart policy if one was configured. Normal sandbox
containers must use `--restart=no` so stopping them requires no policy update.

The emergency command accepts only containers labelled
`io.aicage.managed=true`. It resolves the container's immutable ID before acting,
so a name change cannot redirect the stop to an unrelated container. An already
stopped container is a successful no-op. The lower-level `stop <host-init-pid>`
uses a Linux pidfd to kill a verified isolated namespace init and its children.

Policy state is root-owned mode 0600 inside `/run/aicage-firewall` (mode 0700).
It is bound to the network namespace identity and init process start time.
Policy changes and status reads share a host-side lock, so simultaneous toggle
commands cannot leave the saved mode out of sync with the applied rules.
It is never mounted into the AI container. Missing, stale, malformed, or
uncommittable state during a network change stops the workload. A failed kernel
firewall update also stops it. Read-only status failures do not stop the AI.

## Allowed traffic

| Traffic | Internet on | Internet off |
|---|---|---|
| Exact IPv4 localhost `127.0.0.1` | Allowed | Allowed |
| Exact IPv6 localhost `::1` | Allowed | Allowed |
| Configured proxy IPv4, TCP 3128 | Allowed | Blocked |
| Direct public Internet | Blocked | Blocked |
| Host, LAN, and metadata IPs | Blocked | Blocked |
| Docker DNS `127.0.0.11` | Blocked | Blocked |
| Other UDP, external IPv6, forwarding | Blocked | Blocked |

There is no blanket established-connection exception that keeps sessions alive
after the internet switch is turned off. `status` exposes kernel denied-packet
counters. Counters restart when the rules are replaced; they are packet totals,
not a judgement about the AI's intent. The project's separate sensors provide
access-attempt events.

## Integration requirements

- Use an isolated bridge with a trusted HTTP(S) proxy. The proxy must reject
  private, loopback, link-local, multicast, reserved and metadata destinations,
  including DNS answers, and restrict destination ports. This firewall allows
  a connection to the proxy; it does not inspect an encrypted HTTPS tunnel.
- Keep the proxy configuration and the network switch outside the workload.
- Drop `CAP_NET_ADMIN` and `CAP_NET_RAW` and disallow creation/joining of network
  namespaces. Keep Docker's seccomp restrictions enabled.
- Do not expose the Docker socket, host network/PID namespaces, privileged
  devices, or host credentials to the workload. Run the controller on the same
  Linux host as Docker. The emergency CLI uses its local Unix socket explicitly.
- Start the workload gated, install the firewall and access sensors, and release
  it only after those steps succeed. Repeat after every container restart.
- A proxy-aware client is required. Downloads through HTTP(S) work; arbitrary
  direct TCP/UDP traffic is intentionally unavailable.

The library target is `cage_firewall`; the executable target is `cage-firewall`.
Include `cage/firewall.hpp` and call `Guard(pid).apply(Policy{enabled, proxyIp, 3128})`
from a trusted Linux controller. The CLI supplies policy persistence and managed
container validation. `Guard` rejects host/self namespaces and non-init PIDs.

The firewall controls network traffic. Filesystem/process containment belongs
to the container/sandbox layer. Neither layer guarantees protection against
unknown kernel or runtime vulnerabilities. Internet access intentionally allows
data sent to public services through the proxy; it is not an exfiltration filter.
Packets already transmitted before a switch cannot be recalled.

## Build and verify

```sh
cmake -S firewall -B build/firewall -DBUILD_TESTING=ON
cmake --build build/firewall -j2
ctest --test-dir build/firewall --output-on-failure

# Root on the Linux Docker host; builds a small native fixture from scratch.
python3 firewall/tests/integration.py build/firewall/cage-firewall
```

The policy tests also build on Windows. Enforcement and the kill switch require
Linux/WSL2. The live test creates uniquely named disposable containers and a
private bridge without downloading any images or packages, tests proxy-only access, network cutoff on an established TCP
socket, DNS bypass denial, stale-state fail-closed behavior and process-tree
termination, then removes only resources it created. It uses a deterministic
echo endpoint rather than external network availability. Test the project's
actual proxy policy separately before treating the whole sandbox as validated.

References: [Docker firewall behavior](https://docs.docker.com/engine/network/packet-filtering-firewalls/),
[Netfilter/iptables](https://www.netfilter.org/projects/iptables/),
[Linux pidfd signalling](https://man7.org/linux/man-pages/man2/pidfd_send_signal.2.html).
