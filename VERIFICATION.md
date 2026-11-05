# Verification on this workstation

Verified 2026-11-05 on Windows 11 Home with the existing `ShannonDocker` WSL2 distribution and Docker 29.1.3.

- Windows and Linux C++ targets build successfully.
- Firewall policy CTest passes.
- All **13 sandbox integration checks** pass; individual results are in `artifacts/integration-results.json`.
- Interactive terminal test passes: sandbox stdin/stdout are terminals, and output returns intact.
- A command launched through the native Windows executable returns `WINDOWS_LAUNCHER_OK` from inside the container.
- Codex CLI reports version **0.159.2** and **Logged in using ChatGPT** after importing only the requested auth cache.
- The imported auth file has mode **600** inside the container.
- An authenticated, ephemeral Codex turn returns **SANDBOX_READY** and completes successfully.
- Downloading and installing `jq` through `apt-get` succeeds inside the workload; `jq --version` returns **jq-1.7**.

During the authenticated smoke test, Codex reported a model-catalog refresh timeout, then successfully completed the requested turn. This is a nonblocking warning observed during that test, not evidence that every model-selection path has been verified.

The `main` sandbox is left running with internet and sensors **on**. The external monitor is active. Startup configuration and account files remain inside the isolated workload or protected host control directories as described in [README.md](README.md).

These checks demonstrate the tested behaviors. They are not a security audit or proof against unknown kernel, runtime or proxy vulnerabilities. Persistent disk usage currently has no hard quota; see the README's security limits.
