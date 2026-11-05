#!/usr/bin/env python3
"""Real Docker/WSL integration checks. Run as root after setup, before auth import."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[1]
binary = root / 'build-linux/aicage'
name = sys.argv[1] if len(sys.argv) > 1 else 'main'
state = Path('/var/lib/aicage') / name
base = [str(binary), '--name', name]
checks = []

def call(*args, ok=True, timeout=90):
    p = subprocess.run(base + list(args), input='', text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=timeout)
    if ok and p.returncode:
        raise RuntimeError(f'{args[0]} failed ({p.returncode}): {p.stdout}')
    return p

def passed(description):
    checks.append(description)
    print('PASS ' + description, flush=True)

try:
    call('internet', 'off')
    call('sensors', 'on')
    call('start')
    os.environ['AICAGE_HOST_SECRET'] = 'must-not-enter-workload'
    probe = call('run', '/usr/local/bin/cage-probe')
    print(probe.stdout, end='', flush=True)
    passed('root can write/install; host files, Docker socket and privileged syscalls blocked')
    call('run', 'test', '!', '-e', '/var/lib/aicage/main/events.jsonl')
    p = call('run', 'bash', '-lc', 'printf tamper >> /aicage/broker', ok=False)
    assert p.returncode != 0
    passed('host audit files are absent and the broker binary is read-only')
    p = call('run', 'curl', '--silent', '--show-error', '--max-time', '8', 'https://example.com', ok=False)
    assert p.returncode != 0, 'Internet worked while disabled'
    passed('Internet OFF blocks proxied HTTPS')
    call('internet', 'on')
    p = call('run', 'curl', '--fail', '--silent', '--show-error', '--max-time', '25', 'https://example.com')
    assert 'Example Domain' in p.stdout
    passed('Internet ON permits public HTTPS downloads')
    for target in ['http://169.254.169.254/', 'http://10.0.0.1/', 'http://127.0.0.1/', 'http://[::1]/', 'http://[::ffff:127.0.0.1]/']:
        p = call('run', 'curl', '--noproxy', '', '--silent', '--show-error', '--max-time', '8', '--output', '/dev/null', '--write-out', '%{http_code}', target)
        assert '403' in p.stdout, f'Private destination was not rejected: {target}: {p.stdout}'
    passed('proxy denies metadata, private LAN, IPv4 and IPv6 loopback destinations')
    p = call('run', 'bash', '-lc', 'printf "#include <iostream>\nint main(){std::cout << 42;}\n" > /workspace/hello.cpp && g++ /workspace/hello.cpp -o /workspace/hello && /workspace/hello')
    assert '42' in p.stdout
    passed('can compile and execute downloaded/user-created programs')
    log = (state / 'events.jsonl').read_text()
    assert 'access_denied' in log and 'boundary_probe' in log and 'network_policy_attempt' in log
    passed('external sensor records filesystem, network and privilege attempts')
    call('sensors', 'off')
    time.sleep(1.5)
    call('run', '/usr/local/bin/cage-probe')
    call('run', 'bash', '-lc', 'cat /host/aicage-sensor-disabled-marker 2>/dev/null || true')
    assert 'aicage-sensor-disabled-marker' not in (state / 'events.jsonl').read_text()
    call('sensors', 'on')
    time.sleep(1.5)
    call('run', 'bash', '-lc', 'cat /host/aicage-sensor-enabled-marker 2>/dev/null || true')
    assert 'aicage-sensor-enabled-marker' in (state / 'events.jsonl').read_text()
    passed('sensor switch changes recording without granting access')
    p = call('run', 'bash', '-lc', 'exit 37', ok=False)
    assert p.returncode == 37
    passed('command exit status is preserved')
    p = subprocess.run(base + ['run', 'python3', '-c',
        'import sys; sys.stdout.write("x"*400000); sys.stdout.flush(); d=sys.stdin.buffer.read(); sys.stderr.write(str(len(d))); sys.exit(7)'],
        input='y'*300000, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
    assert p.returncode == 7 and p.stdout == 'x'*400000 and p.stderr == '300000', (p.returncode, len(p.stdout), p.stderr)
    passed('piped stdin/stdout/stderr remain separate with bidirectional backpressure')
    monitor = int((state / 'monitor.pid').read_text())
    os.kill(monitor, signal.SIGKILL)
    for _ in range(100):
        p = subprocess.run(['docker', '--host', 'unix:///var/run/docker.sock', 'inspect', '--format', '{{.State.Running}}', 'aicage-' + name], capture_output=True, text=True)
        if p.stdout.strip() == 'false':
            break
        time.sleep(0.05)
    else:
        raise AssertionError('Workload survived loss of external monitor')
    passed('unexpected monitor death kills the workload')
    call('start')
    call('run', 'test', '-f', '/workspace/hello.cpp')
    passed('restart restores isolation and retains workspace files')
    call('stop')
    p = call('status')
    assert 'Workload: stopped' in p.stdout
    passed('emergency stop terminates the workload')
finally:
    output = root / 'artifacts'
    output.mkdir(exist_ok=True)
    (output / 'integration-results.json').write_text(json.dumps({'passed': checks, 'count': len(checks)}, indent=2) + '\n')

print(f'{len(checks)} integration checks passed.', flush=True)
