'use strict';

const $ = id => document.getElementById(id);
const storage = {
  get(key, fallback) { try { return JSON.parse(localStorage.getItem(key)) ?? fallback; } catch { return fallback; } },
  set(key, value) { try { localStorage.setItem(key, JSON.stringify(value)); return true; } catch { return false; } }
};
let token = new URLSearchParams(location.hash.slice(1)).get('token') || '';
try { if (token) sessionStorage.setItem('aicage-token', token); else token = sessionStorage.getItem('aicage-token') || ''; } catch {}
if (location.hash) history.replaceState(null, '', location.pathname);
let chats = storage.get('aicage-chats-v1', []).filter(c => c && typeof c.id === 'string' && Array.isArray(c.messages)).slice(0, 30);
let selected = storage.get('aicage-selected-v1', '');
let state = null, active = null, sending = false, controlBusy = false, stopping = false, refreshPending = false;
let statusTimer, toastTimer, currentTheme = storage.get('aicage-theme', 'system');
if (currentTheme !== 'system') document.documentElement.dataset.theme = currentTheme;

function node(tag, className, text) {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (text !== undefined) element.textContent = text;
  return element;
}
function icon(name) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('class', 'icon'); svg.setAttribute('aria-hidden', 'true');
  const use = document.createElementNS(svg.namespaceURI, 'use'); use.setAttribute('href', `#i-${name}`);
  svg.append(use); return svg;
}
function notice(text = '') { $('notice').textContent = text; $('notice').hidden = !text; }
function toast(text) { clearTimeout(toastTimer); $('toast').textContent = text; $('toast').hidden = false; toastTimer = setTimeout(() => $('toast').hidden = true, 3500); }
async function api(path, body) {
  const response = await fetch(path, { method: body === undefined ? 'GET' : 'POST',
    headers: { Authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }), cache: 'no-store' });
  const data = await response.json();
  if (!response.ok) { const error = new Error(data.error || `Request failed (${response.status})`); error.status = response.status; throw error; }
  return data;
}
function currentChat() { return chats.find(c => c.id === selected); }
function createChat() {
  const chat = { id: crypto.randomUUID(), title: 'New conversation', model: $('model').value, thread: '', messages: [], job: null };
  chats.unshift(chat); selected = chat.id; save(); return chat;
}
function save() {
  chats = chats.slice(0, 30);
  // Bound browser storage without dropping the active conversation.
  while (chats.length > 1 && JSON.stringify(chats).length > 1500000) chats.pop();
  if (!storage.set('aicage-chats-v1', chats)) toast('Chat history is full. This conversation remains open, but could not be saved.');
  storage.set('aicage-selected-v1', selected);
}
function busy() { return sending || !!active || !!state?.activeJob; }
function renderHistory() {
  $('history').replaceChildren(); $('no-history').hidden = chats.length > 0;
  for (const chat of chats) {
    const row = node('div', `history-entry${chat.id === selected ? ' selected' : ''}`);
    const button = node('button', 'history-link'); button.append(icon('chat'), node('span', '', chat.title));
    button.title = chat.title; button.disabled = busy() && chat.id !== selected;
    if (chat.id === selected) button.setAttribute('aria-current', 'page');
    button.addEventListener('click', () => { selected = chat.id; $('model').value = chat.model || ''; save(); render(); closePanels(); });
    const remove = node('button', 'icon-button delete-chat'); remove.append(icon('trash')); remove.setAttribute('aria-label', `Remove ${chat.title}`); remove.disabled = busy();
    remove.addEventListener('click', () => {
      if (!confirm('Remove this conversation from this app? Sandbox files and the Codex thread will be kept.')) return;
      chats = chats.filter(c => c.id !== chat.id); if (selected === chat.id) selected = chats[0]?.id || ''; save(); render();
    });
    row.append(button, remove); $('history').append(row);
  }
  $('new-chat').disabled = busy();
}

// Model output is untrusted. This renderer creates text nodes and a small set
// of elements; it never interprets HTML, images, scripts, or raw style markup.
function inlineText(target, text) {
  const pattern = /(`[^`\n]+`|\*\*[^*\n]+\*\*|\[[^\]\n]+\]\(https?:\/\/[^\s)]+\))/g;
  let at = 0;
  for (const match of text.matchAll(pattern)) {
    target.append(document.createTextNode(text.slice(at, match.index)));
    const value = match[0];
    if (value.startsWith('`')) target.append(node('code', '', value.slice(1, -1)));
    else if (value.startsWith('**')) target.append(node('strong', '', value.slice(2, -2)));
    else {
      const parts = /^\[([^\]]+)\]\((.+)\)$/.exec(value); const link = node('a', '', parts[1]);
      try { const url = new URL(parts[2]); if (!['http:', 'https:'].includes(url.protocol)) throw new Error(); link.href = url.href; link.target = '_blank'; link.rel = 'noopener noreferrer'; target.append(link); }
      catch { target.append(document.createTextNode(value)); }
    }
    at = match.index + value.length;
  }
  target.append(document.createTextNode(text.slice(at)));
}
async function copy(text, button) {
  try { await navigator.clipboard.writeText(text); button.lastChild.textContent = 'Copied'; setTimeout(() => button.lastChild.textContent = 'Copy', 1800); }
  catch { toast('Clipboard unavailable. Select the text to copy it.'); }
}
function copyButton(text, className = 'copy-message') {
  const button = node('button', className); button.append(icon('copy'), node('span', '', 'Copy'));
  button.setAttribute('aria-label', 'Copy message text'); button.addEventListener('click', () => copy(text, button)); return button;
}
function markdown(text) {
  const body = node('div', 'message-body'); const lines = text.split('\n');
  for (let i = 0; i < lines.length;) {
    if (lines[i].startsWith('```')) {
      const language = lines[i].slice(3).trim(); const code = []; i++;
      while (i < lines.length && !lines[i].startsWith('```')) code.push(lines[i++]); if (i < lines.length) i++;
      const block = node('div', 'code-block'), head = node('div', 'code-header'), pre = node('pre');
      head.append(node('span', '', language || 'Code'), copyButton(code.join('\n'), ''));
      pre.append(node('code', '', code.join('\n'))); block.append(head, pre); body.append(block); continue;
    }
    if (!lines[i].trim()) { i++; continue; }
    const heading = /^(#{1,4})\s+(.+)/.exec(lines[i]);
    if (heading) { const element = node(`h${Math.min(heading[1].length + 1, 4)}`); inlineText(element, heading[2]); body.append(element); i++; continue; }
    const list = /^\s*(?:([-*])|(\d+)\.)\s+/.exec(lines[i]);
    if (list) {
      const element = node(list[2] ? 'ol' : 'ul');
      while (i < lines.length && /^\s*(?:[-*]|\d+\.)\s+/.test(lines[i])) { const item = node('li'); inlineText(item, lines[i++].replace(/^\s*(?:[-*]|\d+\.)\s+/, '')); element.append(item); }
      body.append(element); continue;
    }
    const paragraph = [];
    while (i < lines.length && lines[i].trim() && !/^```|^#{1,4}\s|^\s*(?:[-*]|\d+\.)\s+/.test(lines[i])) paragraph.push(lines[i++]);
    const element = node('p'); inlineText(element, paragraph.join('\n')); body.append(element);
  }
  return body;
}
function renderMessages(forceScroll = false) {
  const scroller = $('conversation-scroll'); const nearBottom = scroller.scrollHeight - scroller.scrollTop - scroller.clientHeight < 110;
  const opened = new Set([...$('messages').querySelectorAll('details[open]')].map(e => e.dataset.id));
  $('messages').replaceChildren(); const chat = currentChat();
  $('welcome').hidden = !!chat?.messages.length;
  for (const message of chat?.messages || []) {
    if (message.role === 'activity') {
      const details = node('details', 'activity'); details.dataset.id = message.id; details.open = opened.has(message.id);
      const summary = node('summary'); summary.append(icon('terminal'), node('span', '', message.label), node('strong', '', message.status === 'running' ? 'Running' : 'Done'));
      details.append(summary, node('pre', '', message.output || message.label)); $('messages').append(details); continue;
    }
    const article = node('article', `message ${message.role === 'error' ? 'error-message' : message.role}`);
    if (message.role === 'user') article.append(node('div', 'user-bubble', message.text));
    else if (message.role === 'assistant') {
      const heading = node('div', 'assistant-heading'); heading.append(icon('box'), node('span', '', 'Sandbox agent'));
      article.append(heading, markdown(message.text), copyButton(message.text));
    } else article.textContent = message.text;
    $('messages').append(article);
  }
  $('thinking').hidden = !active && !sending;
  if (forceScroll || nearBottom) requestAnimationFrame(() => scroller.scrollTop = scroller.scrollHeight);
}
function renderState() {
  const available = !!state?.available, running = available && state.running;
  const activeElsewhere = !!state?.activeJob && state.activeJob !== active?.id;
  const ready = running && state.internet && !busy();
  $('connection-text').textContent = !state ? 'Connecting' : !available ? 'Unavailable' : running ? 'Running' : 'Stopped';
  $('connection-dot').className = `status-dot ${running ? 'running' : !available && state ? 'error' : ''}`;
  $('workload-dot').className = `status-dot${running ? ' running' : ''}`;
  $('instance-name').textContent = state?.instance ? `${state.instance === 'main' ? 'Main' : state.instance} workspace` : 'Main workspace';
  $('workload-state').textContent = !state ? 'Checking status…' : !available ? 'Controller unavailable' : running ? 'Agent environment is running' : 'Environment is stopped';
  $('monitor-state').textContent = !available ? 'Unknown' : state.monitor ? 'Active' : 'Stopped';
  for (const control of ['internet', 'sensors']) { $(control).setAttribute('aria-checked', String(available && !!state[control])); $(control).disabled = !available || controlBusy || stopping; }
  $('internet-description').textContent = !available ? 'Status unavailable' : state.internet ? 'Public web access enabled' : 'All external connections blocked';
  $('sensors-description').textContent = !available ? 'Status unavailable' : state.sensors ? 'Recording access attempts' : 'Recording is paused';
  $('start').hidden = running; $('start').disabled = !available || controlBusy || stopping;
  $('start').textContent = controlBusy ? 'Working…' : 'Start sandbox';
  $('emergency-stop').disabled = stopping || (!!state && available && !running && !busy());
  $('emergency-stop').lastChild.textContent = stopping ? 'Stopping…' : 'Stop sandbox';
  $('ready-action').hidden = true;
  let hint = 'Checking the sandbox…';
  if (state && !available) hint = 'Sandbox unavailable. Check the controller, then refresh.';
  else if (available && !running) { hint = 'Your sandbox is stopped.'; $('ready-action').hidden = false; $('ready-action').textContent = 'Start sandbox'; $('ready-action').dataset.action = 'start'; }
  else if (available && !state.internet) { hint = 'Internet is off. Codex needs a connection to reply.'; $('ready-action').hidden = false; $('ready-action').textContent = 'Enable internet'; $('ready-action').dataset.action = 'internet'; }
  else if (activeElsewhere) hint = 'An agent turn is running in another window. Stop sandbox to end it.';
  else if (active || sending) hint = 'The agent is working inside your sandbox.';
  else if (ready) hint = 'Connected to your sandbox. Ready when you are.';
  $('readiness-text').textContent = hint;
  $('ready-action').disabled = controlBusy || stopping;
  $('send').disabled = !ready || controlBusy || stopping || !$('prompt').value.trim();
  $('model').disabled = busy();
  $('thinking-label').textContent = state && !state.internet ? 'Waiting for internet…' : 'Agent is working…';
}
function render() { renderHistory(); renderMessages(); renderState(); }
async function refreshStatus() {
  if (refreshPending) return;
  refreshPending = true;
  try {
    state = await api('/api/status');
    if (!state.available) notice(state.error || 'Sandbox controller is unavailable. Run the sandbox setup, then refresh.');
    else if ($('notice').dataset.source === 'connection') { notice(); delete $('notice').dataset.source; }
    const chat = chats.find(c => c.job?.id === state.activeJob);
    if (!active && chat && state.activeJob) { selected = chat.id; active = { ...chat.job, chatId: chat.id }; render(); pollJob(active); }
    if (!active && !state.activeJob) for (const c of chats) if (c.job) { c.job = null; save(); }
  } catch (error) {
    state = { available: false }; $('notice').dataset.source = 'connection';
    notice(error.message === 'Failed to fetch' ? 'The local UI server is disconnected. Relaunch AICage UI to reconnect.' : error.message);
  } finally { refreshPending = false; renderState(); renderHistory(); clearTimeout(statusTimer); statusTimer = setTimeout(refreshStatus, 5000); }
}
async function refreshEvents() {
  $('refresh-events').disabled = true;
  try {
    const data = await api('/api/logs'); $('sensor-events').replaceChildren();
    const entries = data.entries.slice(-5).reverse();
    if (!entries.length) $('sensor-events').append(node('p', 'empty-events', 'No events recorded yet. Access attempts will appear here.'));
    for (const entry of entries) {
      const row = node('div', 'sensor-event'), head = node('div', 'sensor-event-head');
      const time = node('time', '', new Date(entry.time * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }));
      head.append(node('strong', '', entry.type.replaceAll('_', ' ')), time); row.append(head, node('p', '', entry.detail)); $('sensor-events').append(row);
    }
  } catch { $('sensor-events').replaceChildren(node('p', 'empty-events', 'Activity is unavailable. Refresh after the sandbox is ready.')); }
  finally { $('refresh-events').disabled = false; }
}
async function control(action, enabled) {
  if (action !== 'stop' && controlBusy) return;
  if (action === 'stop') stopping = true; else controlBusy = true;
  renderState(); notice();
  try {
    const data = await api('/api/control', { action, ...(enabled === undefined ? {} : { enabled }) }); state = data.state;
    toast(action === 'stop' ? 'Sandbox stopped. Your workspace files are kept.' : action === 'start' ? 'Sandbox started.' : `${action === 'internet' ? 'Internet' : 'Access sensors'} ${enabled ? 'on' : 'off'}.`);
    refreshEvents();
  } catch (error) { notice(error.message); }
  finally { if (action === 'stop') stopping = false; else controlBusy = false; renderState(); refreshStatus(); }
}
async function sendMessage(event) {
  event.preventDefault(); if ($('send').disabled) return;
  const prompt = $('prompt').value.trim();
  if (new TextEncoder().encode(prompt).length > 12000) { notice('This message is too long. Keep it under 12,000 UTF-8 bytes.'); return; }
  let chat = currentChat() || createChat(); chat.model = $('model').value;
  if (!chat.messages.length) chat.title = prompt.replace(/\s+/g, ' ').slice(0, 60);
  chat.messages.push({ role: 'user', text: prompt }); sending = true; $('prompt').value = ''; resizePrompt(); notice(); save(); render(); renderMessages(true);
  try {
    const data = await api('/api/chat', { message: prompt, model: chat.model, thread: chat.thread || '' });
    active = { id: data.job, after: 0, chatId: chat.id }; chat.job = { id: data.job, after: 0 }; state.activeJob = data.job; save(); pollJob(active);
  } catch (error) { chat.messages.push({ role: 'error', text: error.message }); save(); }
  finally { sending = false; render(); }
}
async function pollJob(job) {
  if (active !== job) return;
  const chat = chats.find(c => c.id === job.chatId); if (!chat) return;
  try {
    const data = await api(`/api/events?job=${encodeURIComponent(job.id)}&after=${job.after}`);
    if (data.gap) notice('Some activity was missed while this window was away. The latest events are shown.');
    for (const event of data.events) {
      job.after = event.seq;
      if (event.type === 'thread') chat.thread = event.id;
      else if (event.type === 'message') chat.messages.push({ role: 'assistant', text: event.text });
      else if (event.type === 'error') chat.messages.push({ role: 'error', text: event.message });
      else if (event.type === 'cancelled') chat.messages.push({ role: 'error', text: 'Agent turn stopped.' });
      else if (event.type === 'activity') {
        const id = `${job.id}:${event.id}`; let entry = chat.messages.find(m => m.role === 'activity' && m.id === id);
        if (!entry) { entry = { role: 'activity', id }; chat.messages.push(entry); }
        Object.assign(entry, { label: event.label, status: event.status, output: event.output });
      }
    }
    if (chat.job) chat.job.after = job.after;
    if (data.done) { active = null; chat.job = null; if (state) state.activeJob = ''; refreshStatus(); refreshEvents(); }
    save(); render(); if (!data.done) setTimeout(() => pollJob(job), 400);
  } catch (error) {
    if (error.status === 400) { chat.messages.push({ role: 'error', text: 'This live stream expired. Send a new message to resume the saved Codex thread.' }); active = null; chat.job = null; save(); render(); }
    else { notice('Reply connection interrupted. Reconnecting… Use Stop sandbox to end the agent.'); setTimeout(() => pollJob(job), 2500); }
  }
}
let lastPanelTrigger = null;
function drawerAccessibility() {
  const historyOpen = document.body.classList.contains('history-open'), controlsOpen = document.body.classList.contains('controls-open');
  const drawerOpen = historyOpen || controlsOpen;
  $('history-panel').inert = (innerWidth <= 760 && !historyOpen) || controlsOpen;
  $('controls-panel').inert = (innerWidth <= 1280 && !controlsOpen) || historyOpen;
  document.querySelector('.main').inert = drawerOpen;
}
function closePanels(restoreFocus = true) {
  document.body.classList.remove('history-open', 'controls-open'); $('scrim').hidden = true;
  $('open-history').setAttribute('aria-expanded', 'false'); $('open-controls').setAttribute('aria-expanded', 'false');
  drawerAccessibility(); if (restoreFocus && lastPanelTrigger) lastPanelTrigger.focus(); lastPanelTrigger = null;
}
function openPanel(kind) {
  closePanels(false);
  if (kind === 'controls' && innerWidth > 1280) { $('internet').focus(); return; }
  document.body.classList.add(`${kind}-open`); $('scrim').hidden = false;
  lastPanelTrigger = $(`open-${kind}`); lastPanelTrigger.setAttribute('aria-expanded', 'true'); drawerAccessibility(); $(`close-${kind}`).focus();
}
function resizePrompt() { $('prompt').style.height = 'auto'; $('prompt').style.height = `${Math.min($('prompt').scrollHeight, 170)}px`; renderState(); }
$('composer').addEventListener('submit', sendMessage);
$('prompt').addEventListener('input', resizePrompt);
$('prompt').addEventListener('keydown', event => { if (event.key === 'Enter' && !event.shiftKey && !event.isComposing) { event.preventDefault(); if (!$('send').disabled) $('composer').requestSubmit(); } });
$('new-chat').addEventListener('click', () => { if (busy()) return; selected = ''; save(); render(); closePanels(); $('prompt').focus(); });
$('model').addEventListener('change', () => { const chat = currentChat(); if (chat) { chat.model = $('model').value; save(); } });
$('start').addEventListener('click', () => control('start'));
$('internet').addEventListener('click', () => control('internet', !state?.internet));
$('sensors').addEventListener('click', () => control('sensors', !state?.sensors));
$('emergency-stop').addEventListener('click', () => control('stop'));
$('ready-action').addEventListener('click', () => control($('ready-action').dataset.action, $('ready-action').dataset.action === 'internet' ? true : undefined));
$('refresh-events').addEventListener('click', refreshEvents);
$('open-history').addEventListener('click', () => openPanel('history'));
$('open-controls').addEventListener('click', () => openPanel('controls'));
$('close-history').addEventListener('click', () => { closePanels(); $('open-history').focus(); });
$('close-controls').addEventListener('click', () => { closePanels(); $('open-controls').focus(); });
$('scrim').addEventListener('click', closePanels);
$('theme').addEventListener('click', () => { const dark = currentTheme === 'dark' || (currentTheme === 'system' && matchMedia('(prefers-color-scheme:dark)').matches); currentTheme = dark ? 'light' : 'dark'; document.documentElement.dataset.theme = currentTheme; storage.set('aicage-theme', currentTheme); });
for (const button of document.querySelectorAll('[data-prompt]')) button.addEventListener('click', () => { $('prompt').value = button.dataset.prompt; resizePrompt(); $('prompt').focus(); });
document.addEventListener('keydown', event => {
  if (event.key === 'Escape') closePanels();
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'k') { event.preventDefault(); $('new-chat').click(); }
  // Drawers protect focus on narrow screens; Escape and the scrim dismiss them.
  if (event.key === 'Tab' && !$('scrim').hidden) {
    const panel = document.body.classList.contains('history-open') ? $('history-panel') : $('controls-panel');
    const elements = [...panel.querySelectorAll('button:not(:disabled), a, select')].filter(el => el.getClientRects().length);
    if (!elements.length) return;
    if (event.shiftKey && document.activeElement === elements[0]) { event.preventDefault(); elements.at(-1).focus(); }
    else if (!event.shiftKey && document.activeElement === elements.at(-1)) { event.preventDefault(); elements[0].focus(); }
  }
});
window.addEventListener('resize', () => { if (innerWidth > 1280) closePanels(); else drawerAccessibility(); });
window.addEventListener('beforeunload', event => { if (active) { event.preventDefault(); event.returnValue = ''; } });
if (currentChat()) $('model').value = currentChat().model || '';
drawerAccessibility(); render(); refreshStatus(); refreshEvents(); setInterval(refreshEvents, 15000);
