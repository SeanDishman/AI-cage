# Sandbox chat

Operate surface for the owner of a local AI sandbox. Primary task: send a message and follow the agent's work. Persistent controls expose network state, sensor recording, and emergency stop. Code-led implementation; no approved visual comp. The user's ChatGPT-like reference pins the conventional chat arrangement and takes precedence over alternative visual concepts.

The user explicitly requested a standalone Electron desktop application for Windows and OBS capture. Preserve the chat layout inside a native window with its own taskbar entry, normal window controls, remembered bounds and a dedicated app history. Compact desktop windows retain the existing controls drawer. Normal launch opens the desktop app; the browser mode is optional development tooling.

## Direction contract
THESIS: A familiar conversation window with the environment's controls always one click away. The transcript is the dominant surface; technical monitoring stays in a separate, readable side panel.

OWN-WORLD: Neutral light and dark surfaces, Segoe UI system typography, dark primary buttons, muted green running indicators, red reserved for stop/errors. Compact outlined icons, quiet separators, generous conversation measure, rounded composer.

STORY: See whether the sandbox is ready, enable the required connection, send a task, watch actual tool activity, read the reply, continue the same sandbox thread.

FIRST VIEWPORT: A 232px history rail, central conversation with a 740px reading measure, and a 280px sandbox panel on wide screens. The composer anchors the bottom. Narrow windows turn side panels into drawers. Signature interaction: sending moves the welcome view into a persistent transcript without moving the composer.

FORM: User-pinned ChatGPT-style canon; seed 195b6a17 was evaluated but its alternative world/topology cannot override the explicit familiar-chat request. No invented live data. Scene: long desktop sessions under changing ambient light; honor OS theme and offer a manual toggle.

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance

## Quality bar
Readable message hierarchy, working keyboard composer, visible loading/error/offline states, no horizontal page overflow at 390px, confirmed control results, and no HTML execution from model messages. No shipping raster assets are needed for this text-and-controls interface.
