# UI verification — 2026-11-05

The Windows C++20 UI server builds successfully with CMake/Ninja and GCC 16.1. The real sandbox controller remains a separate component owned by the virtualization implementation.

## Electron desktop conversion

The Windows Electron 45.2.3 application was packaged as `ui/dist/AICage-win32-x64/AICage Desktop.exe`. `Open AICage.cmd` and `ui/Launch UI.cmd` launch its own native window, with a hidden C++ helper and no browser launch. The package includes the frontend and helper, and points to the configured controller in this workspace.

Nine actual Electron integration groups passed using the explicitly synthetic controller:

- Renderer sandbox/context isolation enabled; Node and host bridge absent.
- Chat, command activity and resumed conversations routed through C++.
- Sensor controls reflect the controller's confirmed state.
- External navigation, file navigation, popups and device permissions blocked.
- Second launch focuses the existing application.
- Full and compact window layouts remain usable.
- App history and window size survive closing and reopening; helper exits.
- Closing during a reply can be cancelled or can stop the sandbox and close after confirmation.
- Abrupt termination of Electron's actual main process ends its C++ helper.

Evidence: `ui/test-results/desktop-results.json`, `desktop-electron.png`, and `desktop-electron-compact.png`. The focused desktop review inspected the full/compact captures and relevant lifecycle/security integration. It caught the frontend's stale browser unload guard preventing a confirmed close; the desktop now overrides that guard only after its close checks succeed. The crash test targets Electron's actual main PID, which differs from Playwright's launcher PID on this machine.

`npm run test:desktop:live` also passed using the **packaged executable**, real provisioned sandbox and existing account. GPT-6 Luna returned `ELECTRON_READY`; evidence is `ui/test-results/desktop-live.json` and `desktop-live.png`. The prompt requested no tools or workspace changes. The real sandbox was left running with its internet and sensor settings intact. Later close-handling corrections were verified by the final nine-group desktop suite.

The packaged app is a local, unsigned build tied to this workspace, not an installer for arbitrary machines. Electron's renderer restrictions supplement the existing C++/WSL/container boundary; they do not prove immunity to runtime vulnerabilities. An abrupt UI crash closes the helper but is not an emergency-stop command for the sandbox.

## Real account test

`node ui/tests/live.mjs` passed against the provisioned `main` sandbox using the account already imported by the sandbox controller. The browser selected **gpt-6-luna**, submitted a short instruction, and displayed `UI_LUNA_READY`. A second browser message resumed the saved Codex thread and returned the same marker from the previous turn. Neither prompt requested tools or workspace changes. The check did not change network/sensor settings or stop the real sandbox.

Evidence: `ui/test-results/live.json` and `ui/test-results/live-chat.png`. These generated local artifacts are ignored by version control. The opt-in live test uses two short model turns and can be rerun when account verification is needed.

## Automated synthetic checks

Nine API groups passed with the explicit test controller:

- Session authorization, Origin, Fetch Metadata, and Host checks.
- Static asset restriction, Content Security Policy, and no-store headers.
- Stopped/offline chat preconditions and controller-backed switches.
- Literal Unicode and shell metacharacter argument handling; JSON events.
- Conversation resume and explicit account-default model selection.
- Agent/model error propagation.
- Message size, model, thread, and action validation.
- Busy-turn emergency cancellation and refusal to auto-restart afterward.
- Sensor events relayed from controller output.

Seven Chrome browser groups passed: start/connect controls, conversation and command activity, Unicode follow-up, saved history after reload, untrusted HTML kept as text, emergency stop during a turn, responsive drawers/focus/theme, and no JavaScript/CSP errors (related checks are grouped in the test output).

## Visual checks

Desktop at 1440×960 and narrow layout at 390×844 were captured, opened, and checked. The final captures include empty chat, conversation, dark theme, narrow chat, and narrow controls. The Impeccable detector's small-text findings were fixed to a 12px minimum. The working indicator pulses only while a real turn is active; reduced motion disables it.

A fresh delegated finish reviewer inspected the five final screenshots and the relevant UI integration paths, returned **ship**, and requested no material fixes. This used a generic delegated agent because the harness has no custom reviewer-type selector. Its backend check was limited to the UI integration paths; this is not a claim of a comprehensive security audit.

The synthetic tests do not independently prove the container's isolation. See the project's main `VERIFICATION.md` for the actual containment checks. No test can guarantee immunity to future OS, runtime, or browser vulnerabilities.
