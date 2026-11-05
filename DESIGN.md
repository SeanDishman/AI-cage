---
name: "AICage"
description: "A familiar conversation window for a local sandbox agent."
colors:
  bg: "#fff"
  rail: "#f6f7f8"
  panel: "#fafbfc"
  ink: "#202327"
  muted: "#626971"
  line: "#e3e6e9"
  soft: "#eef0f2"
  accent: "#24644f"
  accent-soft: "#e7f2ec"
  danger: "#ab3439"
  danger-soft: "#faeeee"
  button: "#24272b"
  button-ink: "#fff"
  focus: "#28765d"
  dark-bg: "#1d1f22"
  dark-rail: "#16181b"
  dark-panel: "#191b1e"
  dark-ink: "#eceef0"
  dark-muted: "#a7adb5"
  dark-line: "#34383e"
  dark-soft: "#2b2e33"
  dark-accent: "#a1ceb9"
  dark-accent-soft: "#233e33"
  dark-danger: "#f0a4a8"
  dark-danger-soft: "#42292d"
  dark-button: "#eceef0"
  dark-button-ink: "#202327"
  dark-focus: "#9ac9b1"
  scrim: "#10151b55"
typography:
  title:
    fontFamily: "\"Segoe UI\", -apple-system, BlinkMacSystemFont, sans-serif"
    fontSize: "32px"
    fontWeight: 600
    lineHeight: 1.2
    letterSpacing: "-1px"
  headline:
    fontFamily: "\"Segoe UI\", -apple-system, BlinkMacSystemFont, sans-serif"
    fontSize: "19px"
    fontWeight: 600
    lineHeight: 1.4
  body:
    fontFamily: "\"Segoe UI\", -apple-system, BlinkMacSystemFont, sans-serif"
    fontSize: "14px"
    fontWeight: 400
    lineHeight: 1.5
  conversation:
    fontFamily: "\"Segoe UI\", -apple-system, BlinkMacSystemFont, sans-serif"
    fontSize: "15px"
    fontWeight: 400
    lineHeight: 1.75
  navigation:
    fontFamily: "\"Segoe UI\", -apple-system, BlinkMacSystemFont, sans-serif"
    fontSize: "13px"
    fontWeight: 400
    lineHeight: 1.5
  label:
    fontFamily: "\"Segoe UI\", -apple-system, BlinkMacSystemFont, sans-serif"
    fontSize: "12px"
    fontWeight: 400
    lineHeight: 1.5
  action:
    fontFamily: "\"Segoe UI\", -apple-system, BlinkMacSystemFont, sans-serif"
    fontSize: "12px"
    fontWeight: 600
    lineHeight: 1.5
  code:
    fontFamily: "Consolas, monospace"
    fontSize: "13px"
    fontWeight: 400
    lineHeight: 1.65
rounded:
  inline: "4px"
  compact: "8px"
  control: "9px"
  card: "10px"
  composer: "18px"
  composer-narrow: "15px"
  switch: "20px"
  circle: "50%"
spacing:
  micro: "4px"
  tight: "8px"
  control: "10px"
  compact: "12px"
  body: "14px"
  standard: "16px"
  section: "20px"
  gutter: "32px"
components:
  button-primary:
    backgroundColor: "{colors.button}"
    textColor: "{colors.button-ink}"
    typography: "{typography.action}"
    rounded: "{rounded.compact}"
    padding: "9px"
  button-stop:
    textColor: "{colors.danger}"
    typography: "{typography.action}"
    rounded: "{rounded.compact}"
    padding: "10px"
  button-stop-hover:
    backgroundColor: "{colors.danger-soft}"
  button-icon:
    textColor: "{colors.muted}"
    rounded: "{rounded.compact}"
    width: "36px"
    height: "36px"
  button-ghost-hover:
    backgroundColor: "{colors.soft}"
  button-send:
    backgroundColor: "{colors.button}"
    textColor: "{colors.button-ink}"
    rounded: "{rounded.card}"
    width: "34px"
    height: "34px"
  button-new-chat:
    backgroundColor: "{colors.bg}"
    textColor: "{colors.ink}"
    rounded: "{rounded.control}"
    padding: "11px 10px"
  composer:
    backgroundColor: "{colors.bg}"
    textColor: "{colors.ink}"
    rounded: "{rounded.composer}"
    padding: "16px 16px 10px"
  history-item:
    textColor: "{colors.ink}"
    typography: "{typography.navigation}"
    rounded: "{rounded.compact}"
    padding: "10px 9px"
  history-item-selected:
    backgroundColor: "{colors.soft}"
  status-control:
    textColor: "{colors.ink}"
    typography: "{typography.label}"
    rounded: "{rounded.compact}"
    padding: "8px 10px"
  switch-off:
    backgroundColor: "{colors.muted}"
    rounded: "{rounded.switch}"
    padding: "3px"
    width: "34px"
    height: "20px"
  switch-on:
    backgroundColor: "{colors.accent}"
  activity-disclosure:
    textColor: "{colors.muted}"
    typography: "{typography.label}"
    rounded: "{rounded.control}"
  code-block:
    backgroundColor: "{colors.rail}"
    textColor: "{colors.ink}"
    typography: "{typography.code}"
    rounded: "{rounded.card}"
  user-bubble:
    backgroundColor: "{colors.soft}"
    textColor: "{colors.ink}"
    rounded: "18px 18px 4px 18px"
    padding: "12px 17px"
---

# Design System: AICage

## Overview

**Creative North Star: "A familiar conversation window"**

AICage uses the familiar chat arrangement requested for its local sandbox agent: neutral surfaces, readable conversation, restrained controls, and compact outlined icons. The transcript carries the visual weight; environment controls remain available beside it or in a drawer.

Light and dark themes preserve the same hierarchy. Tonal surfaces and quiet separators organize the workspace, while rounded controls and the composer provide a gentle, practical form language. The built interface uses text and inline SVG, with no shipping raster imagery.

**Key Characteristics:**

- Neutral light and dark surfaces with muted green state cues and red stop/error cues.
- Segoe UI text for the Windows workspace, with Consolas for code and command output.
- An open assistant transcript, softly filled user messages, and a persistent rounded composer.
- Side rails on wide screens, accessible drawers in narrower windows, and restrained state motion.

This record describes the built interface in `ui/web/index.html`, `ui/web/app.css`, and `ui/web/app.js`. The existing direction in `ui/design-brief.md` supplies the descriptive language; no visual comp was approved. The frontmatter owns the extracted primitive values, while `.impeccable/design.json` carries motion, shadows, breakpoints, component previews, and narrative.

## Colors

The palette is quiet and neutral, with restrained green for positive state and red for interruption or error.

### Primary

- **Neutral action:** The `button` and `button-ink` pair gives primary actions a charcoal fill in light mode and a pale fill in dark mode.
- **Muted green:** `accent` marks running indicators, enabled switches, and links. `accent-soft` supplies the selection fill; `focus` supplies keyboard outlines and the text caret.
- **Stop red:** `danger` marks stop actions and error text; `danger-soft` supplies error surfaces and the stop-button hover.

### Neutral

| Role | Light token | Dark token | Applied role |
| --- | --- | --- | --- |
| Canvas | `bg` | `dark-bg` | Main transcript, composer, and switch thumb |
| History rail | `rail` | `dark-rail` | History background and code blocks |
| Sandbox panel | `panel` | `dark-panel` | Environment controls |
| Main text | `ink` | `dark-ink` | Conversation and primary labels |
| Supporting text | `muted` | `dark-muted` | Hints, metadata, inactive status, and icons |
| Separator | `line` | `dark-line` | Panel, field, and disclosure borders |
| Soft fill | `soft` | `dark-soft` | User bubbles, selected history, hover, and inline code |
| Primary action | `button` / `button-ink` | `dark-button` / `dark-button-ink` | Start, send, and toast |
| Positive state | `accent` / `accent-soft` | `dark-accent` / `dark-accent-soft` | Status, switch, links, and selection |
| Stop/error | `danger` / `danger-soft` | `dark-danger` / `dark-danger-soft` | Stop and errors |
| Focus | `focus` | `dark-focus` | Focus outline and caret |

The `scrim` token dims the background behind either drawer. It remains the same in both themes. Initial appearance follows the operating system. The theme control switches to an explicit light or dark choice and saves that choice in browser storage; the current UI does not expose a reset-to-system option.

**The Theme Pairing Rule.** Keep semantic color roles paired across light and dark themes; components consume the active CSS custom properties.

**The Status Color Rule.** Use green for running, enabled, and focus states; use red for stop actions and errors. Ordinary primary actions use the neutral button pair.

The source also declares `--hover`, but no component consumes it. It is intentionally excluded from the normative color set. Preview tonal ramps in the sidecar are generated swatch metadata, not additional shipping palette tokens.

## Typography

**UI font:** Segoe UI, followed by the platform sans-serif fallbacks in the frontmatter.  
**Code font:** Consolas, followed by monospace.

The type is direct and practical for a Windows workspace. Hierarchy comes from scale, weight, and spacing within the same UI family; code and command output use a separate fixed-width face.

### Hierarchy

- **Welcome title:** The `title` token is used by the welcome heading. At the narrow breakpoint its size reduces to (27px) with tracking of (-0.8px); short windows also use the smaller size.
- **Transcript headings:** The `headline` token applies consistently to supported reply headings.
- **Interface body:** The `body` token establishes the base controls and shell text.
- **Conversation:** The `conversation` token makes assistant replies larger and more open. User bubbles use the same size with a tighter line-height (1.65). Both reduce to (14px) in narrow windows.
- **Navigation:** The `navigation` token describes conversation titles and starter actions. The new-conversation action adds weight (550).
- **Labels and actions:** The `label` token describes status, hints, and metadata. The `action` token gives compact action labels their stronger weight.
- **Code:** The `code` token describes code blocks. Command details use (12px) with line-height (1.6); inline code uses (13px). Event timestamps use tabular numerals.
- **Local emphasis:** The model selector uses (17px, weight 600), becoming (15px) on narrow screens. The product wordmark uses (20px, weight 650). Assistant attribution uses (13px, weight 650), and the sandbox heading uses (15px, weight 650).

**The Conversation Hierarchy Rule.** Keep conversation text larger and more open than the compact navigation, status, and control labels.

## Layout

The shell fills the dynamic viewport height (100dvh), with the page itself fixed and its content regions scrolling independently. A fixed-width history rail (232px) and sandbox panel (280px) flank a flexible, minimum-width-zero conversation column on wide screens.

The conversation measure is capped at (740px). Its side gutters are (32px), and the bottom composer wrapper has a matching outer cap (804px). The top bar is (76px) high. The welcome content caps at (620px), with a compact vertical list of starter actions. Messages have generous separation (30px), while side-panel labels and controls use the tighter spacing steps in the frontmatter. These are extracted recurring values rather than a rigid arithmetic spacing scale.

The composer is a nonshrinking sibling of the transcript scroller. Sending replaces the welcome content with the transcript without moving the composer into that scroller. The textarea grows with its content up to (170px), then scrolls within itself.

- **At or below 1280px:** The sandbox panel becomes a right drawer (300px wide). Its opener remains in the top bar; the secondary model caption disappears.
- **At or below 760px:** The history rail becomes a left drawer (264px wide). The top bar becomes (65px) high, conversation gutters shrink to (20px), and composer gutters shrink to (14px). User bubbles may occupy (92%) of the message width instead of (85%). Readiness text wraps, and history delete controls remain visible.
- **At or below 680px viewport height:** The welcome section tightens its vertical spacing and uses the smaller title.
- **Overflow:** Long conversation text can wrap anywhere. Code blocks retain their own horizontal scrolling. Drawer content and control-panel content can scroll independently.

Only one drawer opens at a time. The drawer uses a scrim, makes background content inert, moves focus to its close control, traps Tab within the drawer, and restores focus when dismissed. Escape and the scrim dismiss it.

## Elevation & Depth

Most depth comes from tonal layers and fine borders (1px), with open assistant messages on the canvas. The composer has a faint ambient shadow; the transient toast has a stronger diffuse shadow. The drawer uses a scrim and stacking rather than a new shadow style.

### Shadow Vocabulary

- **Composer:** `0 3px 14px #17202b08`, shared by both themes.
- **Toast, light:** `0 8px 32px #16222d14`, from the active shadow variable.
- **Toast, dark:** `0 8px 32px #0003`, from the dark theme shadow variable.

The scrim sits at layer (3), drawers at (4), and toast at (10).

**The Quiet Depth Rule.** Use tonal separation and fine borders for the workspace. Reserve the observed diffuse shadows for the composer and transient toast.

## Shapes

Controls use gently curved corners. Compact buttons and navigation use the `compact` radius; new conversation, environment icons, and command disclosures use `control`; code blocks, starter actions, toast, and send use `card`. The composer uses the larger `composer` curve, becoming `composer-narrow` in narrow windows. Inline code uses `inline`.

User bubbles have an asymmetric lower-right corner, as recorded by their component token. Switch tracks are pill-shaped and their thumbs are circular. Status dots are circular, small, and paired with text.

Icons are inline SVG outlines, usually (20px), with rounded ends and joins and a stroke width of (1.65). Compact control icons range from (14px) to (18px); the welcome mark is a larger (40px) version with a thinner stroke (1.3). The box motif belongs to AICage and does not use an OpenAI logo.

## Components

### Buttons

Compact, quiet controls with a clear hierarchy.

- **Primary:** Start and send use the neutral action color pair. Hover retains that pair and applies brightness (0.85).
- **Outlined:** New conversation uses the canvas fill and separator border. Stop uses a danger border and text, with the soft danger fill on hover.
- **Ghost:** Icon, starter, copy, and text actions gain the soft fill on hover. Icon buttons generally occupy (36px) squares.
- **Focus:** Interactive controls use a focus-colored outline (2px) offset by (3px). The composer textarea is the exception: its enclosing border changes on focus.
- **Disabled:** Controls use a not-allowed cursor and opacity (0.45); the disabled send button uses opacity (0.3).
- **Copy:** Message and code copy actions temporarily show “Copied”; a toast explains clipboard failure.

### Inputs / Fields

The composer is a curved, bordered canvas surface with a faint shadow. Its border changes to the muted color on focus-within over (0.18s). The borderless textarea uses readable text (15px, line-height 1.55), a muted placeholder, and the focus-colored caret.

Enter sends; Shift + Enter inserts a new line; composing input is respected. Readiness text above the field explains checking, stopped, offline, busy, and ready states and supplies the relevant recovery action. Send remains disabled until the environment is ready and the field contains text. Starter actions fill and focus this field.

The model selector is a native select with a transparent surface. It offers GPT-6 Luna and the Codex default and is disabled during an active turn.

### Navigation

Conversation history is a compact text list with outlined chat icons. The selected row uses the soft fill and exposes its current state accessibly. Long titles truncate visually while retaining a title tooltip. Delete controls appear on hover or keyboard focus on wide screens and stay visible in narrow windows. Removal asks for confirmation.

New conversation clears the active view and focuses the composer; Ctrl/Cmd + K activates it. While a turn runs, switching to another conversation, deletion, and new conversation are disabled. Existing local history and its selected conversation survive reloads.

### Transcript and Containers

Assistant replies sit directly on the canvas beneath a compact “Sandbox agent” attribution. User text is right-aligned in a soft bubble. Error messages use a rounded danger-soft surface and danger text.

Supported replies include paragraphs, lists, headings, bold, inline code, fenced code, and HTTP(S) links. Code blocks use a rail-colored fill, a fine border, a separate language/copy header, and a scrolling monospace body. Command activity is a bordered disclosure with a compact summary and an optional output area capped at (260px). Its open state survives transcript refreshes.

The pending-reply indicator pairs readable status text with a green dot. Its opacity breathes over (1.6s), with an ease-in-out curve. The transcript follows new content when already near the bottom; sending explicitly scrolls to the newest message.

### Sandbox Controls and Status

A bordered status control in the top bar combines a dot and readable state. Green marks running; muted marks stopped or pending; red marks unavailable. On wide screens its action focuses the environment controls; narrower screens open the drawer.

Internet and sensor controls use switches with explicit accessible checked state. The track transitions over (0.2s); the thumb travels (14px) with `cubic-bezier(.16,1,.3,1)`. Controls show pending/disabled states and use confirmed responses to update their state. A transient toast reports successful actions.

Recent sensor activity is a divided list with event names, compact text, and tabular timestamps. Empty and unavailable states remain text. The stop action remains at the bottom of the controls region and explains that workspace files are retained.

### Motion and Reduced Motion

Drawers translate over (0.22s) with the same easing curve as switch movement. Other observed transitions are limited to composer focus, switch state, and the working indicator; scrolling is smooth by default. Under `prefers-reduced-motion: reduce`, animation and transitions are disabled and scrolling becomes immediate. Motion does not carry the only indication of state.

## Do's and Don'ts

### Do:

- Do use the active theme variables for surfaces, text, borders, controls, and state colors.
- Do keep assistant replies on the open canvas and user messages in the soft filled bubble.
- Do pair status color with readable status text, and show confirmed control results.
- Do preserve the composer position, independent transcript scrolling, keyboard input, and drawer focus behavior.
- Do use the existing outlined SVG icon language and respect reduced motion.

### Don't:

- Don't use green as the normal primary-button fill or red as decoration.
- Don't hide the input or sandbox stop control behind unrelated navigation.
- Don't introduce OpenAI logos or present AICage as an official OpenAI interface.
- Don't turn model-provided HTML or images into interface content; preserve the implemented text and supported Markdown rendering.

