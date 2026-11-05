#!/usr/bin/env python3
"""Exercise the real firewall in disposable containers; never alter host rules.

Run as root on the Linux Docker host:
  python3 firewall/tests/integration.py /path/to/cage-firewall
Builds a native static fixture from scratch; no images or packages are downloaded.
"""

import json
import os
import pathlib
import select
import subprocess
import sys
import tempfile
import time
import uuid


def run(*arguments, expected=0, timeout=30):
    result = subprocess.run(arguments, text=True, capture_output=True, timeout=timeout)
    if expected is not None and result.returncode != expected:
        raise AssertionError(f"{arguments[0]} returned {result.returncode}: {result.stderr}\n{result.stdout}")
    return result


def docker(*arguments, **options):
    return run("docker", "--host", "unix:///var/run/docker.sock", *arguments, **options)


def inspect(name):
    return json.loads(docker("inspect", name).stdout)[0]


def passed(description):
    print(f"PASS {description}", flush=True)


def probe(pid, address, port, allowed):
    code = """
import socket,sys
s=socket.socket(); s.settimeout(1.0)
try:
    s.connect((sys.argv[1],int(sys.argv[2])))
    s.sendall(b'cage-firewall-test')
    assert s.recv(64)==b'cage-firewall-test'
except (OSError,AssertionError):
    sys.exit(0 if sys.argv[3]=='denied' else 1)
sys.exit(0 if sys.argv[3]=='allowed' else 1)
"""
    run("nsenter", "-t", str(pid), "-n", sys.executable, "-c", code,
        address, str(port), "allowed" if allowed else "denied")


def main():
    if len(sys.argv) != 2 or os.geteuid() != 0:
        raise SystemExit("Usage (root on Linux Docker host): integration.py /path/to/cage-firewall")
    binary = str(pathlib.Path(sys.argv[1]).resolve(strict=True))
    tag = "cage-fw-test-" + uuid.uuid4().hex[:10]
    network = tag + "-net"
    created = []
    processes = []
    state_pids = []
    network_created = False
    image_created = False
    try:
        with tempfile.TemporaryDirectory(prefix="cage-firewall-fixture-") as temporary:
            fixture = pathlib.Path(temporary)
            run("g++", "-std=c++20", "-static", "-O2", str(pathlib.Path(__file__).with_name("fixture.cpp")),
                "-o", str(fixture / "fixture"), timeout=90)
            (fixture / "Dockerfile").write_text("FROM scratch\nCOPY fixture /fixture\nENTRYPOINT [\"/fixture\"]\n")
            docker("build", "--network", "none", "-t", tag, temporary, timeout=90)
            image_created = True
        docker("network", "create", "--internal", "--label", "io.aicage.firewall-test=true", network)
        network_created = True

        def create(suffix, managed=True):
            name = tag + "-" + suffix
            arguments = ["run", "-d", "--name", name, "--network", network, "--cap-drop", "ALL",
                         "--security-opt", "no-new-privileges=true", "--pids-limit", "32",
                         "--memory", "64m", "--label", "io.aicage.firewall-test=true"]
            if managed:
                arguments += ["--label", "io.aicage.managed=true"]
            result = docker(*arguments, tag, "echo" if suffix == "proxy" else "hold")
            container_id = result.stdout.strip()
            created.append(container_id)
            return container_id, inspect(container_id)

        proxy_id, proxy = create("proxy")
        workload_id, workload = create("workload")
        workload_pid = workload["State"]["Pid"]
        proxy_pid = proxy["State"]["Pid"]
        state_pids.append(workload_pid)
        proxy_ip = proxy["NetworkSettings"]["Networks"][network]["IPAddress"]
        gateway = workload["NetworkSettings"]["Networks"][network]["Gateway"]
        assert proxy_pid > 1 and workload_pid > 1, "Fixture did not start"
        # Deterministic native TCP echo fixture; the actual Squid policy is
        # tested by the sandbox integration separately.
        time.sleep(0.2)

        for target in ("1", str(os.getpid())):
            result = run(binary, "setup", target, proxy_ip, "on", expected=None)
            assert result.returncode != 0, "Host namespace was accepted"
        passed("host/self namespace rejected")

        run(binary, "setup", str(workload_pid), proxy_ip, "off")
        probe(workload_pid, proxy_ip, 3128, False)
        passed("network starts blocked")

        run(binary, "set", str(workload_pid), "on")
        probe(workload_pid, proxy_ip, 3128, True)
        probe(workload_pid, proxy_ip, 3129, False)
        passed("only the configured proxy port is reachable")

        for address in filter(None, [gateway, "169.254.169.254", "1.1.1.1", "10.0.0.1", "192.168.1.1"]):
            probe(workload_pid, address, 443, False)
        passed("direct Internet, bridge host, LAN and metadata traffic blocked")

        dns_code = """
import socket,sys
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.settimeout(1)
query=b'\\x12\\x34\\x01\\x00\\x00\\x01\\x00\\x00\\x00\\x00\\x00\\x00\\x07example\\x03com\\x00\\x00\\x01\\x00\\x01'
try:
    s.sendto(query,('127.0.0.11',53))
    s.recv(2048)
except (socket.timeout,PermissionError):sys.exit(0)
sys.exit(1)
"""
        run("nsenter", "-t", str(workload_pid), "-n", sys.executable, "-c", dns_code)
        passed("Docker DNS cannot bypass the firewall")

        # Test an already established connection, not just a new connection.
        stream_code = """
import socket,sys
s=socket.socket();s.settimeout(1);s.connect((sys.argv[1],3128))
s.sendall(b'first');assert s.recv(5)==b'first'
print('CONNECTED',flush=True)
sys.stdin.readline()
s.sendall(b'after-switch')
try:s.recv(12)
except socket.timeout:print('BLOCKED',flush=True);sys.exit(0)
sys.exit(1)
"""
        stream = subprocess.Popen(["nsenter", "-t", str(workload_pid), "-n", sys.executable,
                                   "-u", "-c", stream_code, proxy_ip],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        processes.append(stream)
        assert select.select([stream.stdout], [], [], 10)[0], "Connection was not established"
        assert stream.stdout.readline().strip() == "CONNECTED"
        run(binary, "set", str(workload_pid), "off")
        stream.stdin.write("go\n"); stream.stdin.flush()
        stdout, stderr = stream.communicate(timeout=10)
        assert stream.returncode == 0 and "BLOCKED" in stdout, stderr + stdout
        passed("internet off cuts existing TCP sessions")

        run(binary, "set", str(workload_pid), "on")
        probe(workload_pid, proxy_ip, 3128, True)
        counters = run(binary, "status", str(workload_pid)).stdout
        assert "CAGE_DENIED" in counters and "Internet: on" in counters
        passed("internet can be restored and denied packet counters are available")

        # Deliberately corrupt only this test sandbox's host-side policy file.
        # A stale or missing policy must kill its workload instead of opening it.
        policy_file = pathlib.Path("/run/aicage-firewall") / str(workload_pid)
        original = policy_file.read_text()
        policy_file.write_text("0:0:0\n" + "\n".join(original.splitlines()[1:]) + "\n")
        result = run(binary, "set", str(workload_pid), "on", expected=None)
        assert result.returncode != 0
        assert not inspect(workload_id)["State"]["Running"]
        passed("stale policy fails closed and terminates the workload")

        unmanaged_id, _ = create("unmanaged", managed=False)
        result = run(binary, "kill", unmanaged_id, expected=None)
        assert result.returncode != 0 and inspect(unmanaged_id)["State"]["Running"]
        passed("emergency control refuses unrelated containers")

        stop_id, stop_container = create("stop")
        time.sleep(0.2)
        namespace_pids = [int(line.strip()) for line in docker("top", stop_id, "-eo", "pid").stdout.splitlines()[1:]]
        assert len(namespace_pids) >= 3, "Fixture did not create a process tree"
        run(binary, "kill", stop_id)
        assert not inspect(stop_id)["State"]["Running"]
        deadline = time.monotonic() + 5
        while any(pathlib.Path(f"/proc/{pid}").exists() for pid in namespace_pids) and time.monotonic() < deadline:
            time.sleep(0.05)
        assert not any(pathlib.Path(f"/proc/{pid}").exists() for pid in namespace_pids)
        passed("emergency stop terminates init and all child processes")
        run(binary, "kill", stop_id)
        passed("repeating emergency stop is safe")
        print("All live firewall integration checks passed.", flush=True)
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
        for container in reversed(created):
            # All IDs originate from this test's docker run; no broad prune.
            docker("rm", "-f", container, expected=None)
        if network_created:
            docker("network", "rm", network, expected=None)
        if image_created:
            docker("image", "rm", tag, expected=None)
        for pid in state_pids:
            (pathlib.Path("/run/aicage-firewall") / str(pid)).unlink(missing_ok=True)
            (pathlib.Path("/run/aicage-firewall") / (str(pid) + ".lock")).unlink(missing_ok=True)


if __name__ == "__main__":
    main()
