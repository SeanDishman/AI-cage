<!-- impeccable:product-schema 1 -->
# AICage

## Product purpose
A local interface for talking to a Codex agent that can install software and work inside an isolated Linux environment. The human can control internet access and stop the agent from outside that environment.

## Users and context
The owner uses Windows and wants a familiar ChatGPT-style conversation window in a standalone Electron application. Long desktop sessions and OBS recording are the primary use; compact desktop windows must also work. Follow the operating system's light/dark preference and allow a manual theme toggle.

## Platform and stack
Electron desktop application on Windows, explicitly requested by the user. The existing HTML, CSS, and JavaScript run in a sandboxed renderer without Node integration or a host IPC bridge. Electron owns a hidden C++20 helper serving only loopback; the C++ controller operates WSL2 and Docker. The app opens its own native window rather than the user's browser.

## Capabilities and constraints
Send messages, resume sandbox Codex threads, see command activity and replies, manage local conversation history, view sandbox status and sensor events, toggle internet and sensors, and emergency-stop the sandbox. Every agent action goes through the sandbox controller. UI must never execute agent instructions on Windows. Account credentials remain in the container. No fabricated live readings or agent replies. Internet off prevents cloud Codex replies; emergency stop terminates the workload and retains workspace files. Containers cannot promise immunity to all kernel/runtime flaws.

## Brand commitments
User requested a GPT-style UI: conventional chat layout, restrained controls, and readable conversation. Use the product name AICage, without OpenAI logos or claims of being an official OpenAI interface.

## Open interpretation
“GPT Luna six” is interpreted as gpt-6-luna, offered alongside the account's default model. A structured question was offered; this is an inferred default unless the user corrects it. Direct code implementation is the working assumption for this functional chat request; no visual mockup has been approved.
