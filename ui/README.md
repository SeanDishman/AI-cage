# AICage desktop app

Double-click **Open AICage.cmd** in the project root, or **Launch UI.cmd** in this folder. It opens a standalone **Electron** window titled **AICage**, with its own taskbar entry. For OBS, select this window in Window Capture. The packaged executable is `ui/dist/AICage-win32-x64/AICage Desktop.exe`.

The launcher builds the desktop app if necessary. The sandbox controller must already be built and provisioned using the project setup instructions in [../README.md](../README.md). The desktop package uses this workspace's controller; retain the project location or rebuild after moving it. It is a local build, not a signed installer.

1. Click **Start sandbox** if it is stopped.
2. Turn **Internet** on to connect cloud Codex to OpenAI.
3. Send a message. Follow-up messages resume the same Codex thread inside the sandbox.
4. Use **Stop sandbox** to immediately end the agent and all commands in that environment. Workspace files remain.

The default model is **GPT-6 Luna** (`gpt-6-luna`). Choose **Codex default** to use the model configured for the account. Model availability depends on that account; a rejected model produces an error and is never silently replaced. Account setup belongs to the sandbox controller; this UI does not read or receive sign-in tokens.

Internet off cuts the network but does not terminate local programs. Stop sandbox is the emergency stop. Turning sensors off pauses recording; containment stays active. On narrower windows, open the status button at the top right for these controls.

## Local data and lifecycle

Conversation transcripts are kept in the desktop app's dedicated profile under `%APPDATA%\AICage` (up to 30 conversations, bounded storage). They persist across relaunches, along with window size and position. The Codex thread itself remains in the sandbox. Removing a conversation only removes the desktop copy. Earlier browser history is separate and is not automatically imported. The local helper token stays in process/session memory; account sign-in tokens remain in the sandbox.

Closing an idle desktop window exits its hidden C++ helper and leaves the sandbox running. Use **Stop sandbox** when all sandbox programs should stop. During a reply, closing offers **Keep working** or **Stop sandbox and close**; the latter waits for a confirmed stop. Relaunching focuses the existing app. The helper also exits if Electron crashes, but an abrupt crash is not an emergency-stop command; sandbox programs may continue.

The UI talks only to `build/AICage.exe` with fixed operations: `status`, `logs`, `start`, `stop`, `internet`, `sensors`, and `run codex exec` / `run codex exec resume`. There is no API for executing Windows commands. Prompts are individual process arguments, without shell interpolation; they are limited to 12,000 UTF-8 bytes. Local administrators can inspect process arguments, so this is not a boundary against a hostile Windows administrator.

The desktop helper binds only to `127.0.0.1:43128`, rejects foreign Host/Origin/Fetch Metadata values, requires a random session token for every API, and sends a restrictive Content Security Policy. The Electron renderer has sandboxing and context isolation enabled, Node integration disabled, and no preload/host IPC bridge. External navigation, popups, renderer downloads, and device permissions are blocked. Public downloads made by the agent still go through the sandbox's network policy. Model replies remain text and a limited Markdown subset; HTML and remote images are never executed or loaded. To visit a link outside the app, copy it manually. Chat execution has a 30-minute turn limit, after which sandbox stop is requested and its outcome reported.

Security against sandbox escape is provided by the C++ controller, firewall, monitor, and container boundary described in the project README. This UI does not turn a container into a guarantee against kernel/runtime vulnerabilities.

## Build and checks

From the project root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\ui\build-desktop.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\ui\build.ps1 -Tests
node .\ui\tests\api.mjs
Set-Location .\ui
npm run test:desktop
npm run test:browser
```

The tests use an explicit **synthetic controller fixture**. API tests check authorization, foreign-origin rejection, literal argument passing including Unicode, thread resume, input validation, streaming errors, controls, and emergency cancellation. Browser tests exercise chat, reload, unsafe HTML, mobile drawers, keyboard focus, stop behavior, and theme switching. Browser tests require installed Google Chrome; they use a temporary browser profile and save screenshots under `.impeccable/review/`. They do not access your account or alter the real sandbox.

An additional opt-in `node ui/tests/live.mjs` check uses the real sandbox account for two short GPT-6 Luna turns. It verified the initial reply and a follow-up remembering the earlier message. See [VERIFICATION.md](VERIFICATION.md) for the recorded scope and evidence.

`npm run test:desktop` tests the actual Electron window against the synthetic fixture, including chat/resume, controls, renderer restrictions, relaunch persistence, single instance, active-turn close and helper cleanup after a crash. `npm run test:desktop:live` uses the packaged executable and one short real model turn; it requires the actual sandbox to be running with internet enabled. Close the desktop app before these checks.

For optional browser-based development only (the normal launcher uses Electron):

```powershell
.\ui\build\AICageUI.exe --name main --port 43127 --open
```

Development-only `--controller PATH` and `--assets PATH` select local trusted files at process startup. The browser cannot change either path. The launcher always selects the real project controller; it never selects the fixture.

## Dependencies and protocol

Vendored headers retain their upstream MIT licenses:

| Dependency | Pinned version | SHA-256 of header |
| --- | --- | --- |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib/releases/tag/v0.64.0) | 0.64.0 | `805169049e6a6e9ba7729d7e01d14ced825029c88a277586602a6b38df4d5357` |
| [nlohmann/json](https://github.com/nlohmann/json/releases/tag/v3.12.0) | 3.12.0 | `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63` |

Desktop runtime: Electron 45.2.3. Packaging: `@electron/packager` 20.3.0. Test driver: Playwright 1.64.1. All are pinned in package-lock.json. Node/npm are needed to build, not to launch the packaged executable. The package includes Electron's license notices. Desktop restrictions follow the [Electron security guidance](https://www.electronjs.org/docs/latest/tutorial/security).

Codex JSONL events and session resume follow the [official non-interactive documentation](https://learn.chatgpt.com/docs/non-interactive-mode). Model selection follows the [official model documentation](https://learn.chatgpt.com/docs/models).
