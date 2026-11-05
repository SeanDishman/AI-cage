'use strict';
const { app, BrowserWindow, Menu, dialog, session, screen } = require('electron');
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

app.setName('AICage');
app.setAppUserModelId('local.aicage.desktop');
app.enableSandbox();
// Test overrides are development-only. The packaged app always uses its real
// controller, fixed loopback port and dedicated profile, never a test fixture.
const testing = !app.isPackaged && process.env.AICAGE_DESKTOP_TEST === '1';
app.setPath('userData', testing ? process.env.AICAGE_DESKTOP_TEST_DATA : path.join(app.getPath('appData'), 'AICage'));
const port = testing ? Number(process.env.AICAGE_DESKTOP_TEST_PORT || 43148) : 43128;
if (!Number.isInteger(port) || port < 1024 || port > 65535) throw new Error('Invalid desktop port');
const origin = `http://127.0.0.1:${port}`;
let win, helper, token = '', allowClose = false, closing = false, quitting = false;
let helperReady = false, failed = false;
const ownsLock = app.requestSingleInstanceLock();

function localPage(value) {
  try { const url = new URL(value); return url.origin === origin && url.pathname === '/' && !url.search && !url.username && !url.password; }
  catch { return false; }
}
function configuration() {
  const ui = path.resolve(__dirname, '../..');
  const resources = app.isPackaged ? process.resourcesPath : ui;
  const controller = app.isPackaged
    ? JSON.parse(fs.readFileSync(path.join(resources, 'backend.json'), 'utf8')).controller
    : testing ? process.env.AICAGE_DESKTOP_TEST_CONTROLLER : path.join(ui, '..', 'build', 'AICage.exe');
  const server = path.join(resources, app.isPackaged ? 'AICageUI.exe' : 'build/AICageUI.exe');
  const assets = path.join(resources, 'web');
  if (!controller || !path.isAbsolute(controller) || !fs.existsSync(controller) || !fs.existsSync(server)) {
    throw new Error('The sandbox controller or desktop helper is missing. Rebuild with ui\\build-desktop.ps1.');
  }
  return { controller, server, assets };
}
function saveBounds() {
  if (!win || win.isDestroyed()) return;
  try {
    fs.mkdirSync(app.getPath('userData'), { recursive: true });
    fs.writeFileSync(path.join(app.getPath('userData'), 'window.json'), JSON.stringify({ ...win.getNormalBounds(), maximized: win.isMaximized() }));
  } catch { /* A read-only profile must not prevent closing. */ }
}
function bounds() {
  const area = screen.getPrimaryDisplay().workArea;
  let value = { width: Math.min(1440, area.width), height: Math.min(940, area.height) };
  try {
    const saved = JSON.parse(fs.readFileSync(path.join(app.getPath('userData'), 'window.json'), 'utf8'));
    if (['x', 'y', 'width', 'height'].every(k => Number.isInteger(saved[k])) && saved.width >= 800 && saved.height >= 560 &&
        screen.getAllDisplays().some(d => saved.x >= d.workArea.x && saved.y >= d.workArea.y &&
          saved.x + saved.width <= d.workArea.x + d.workArea.width && saved.y + saved.height <= d.workArea.y + d.workArea.height)) value = saved;
  } catch { /* First launch uses the primary display. */ }
  return value;
}
async function api(route, body) {
  const response = await fetch(origin + route, {
    method: body ? 'POST' : 'GET', signal: AbortSignal.timeout(body ? 60000 : 12000),
    headers: { Authorization: `Bearer ${token}`, ...(body ? { 'Content-Type': 'application/json' } : {}) },
    ...(body ? { body: JSON.stringify(body) } : {})
  });
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || 'The sandbox controller did not respond.');
  return result;
}
async function requestClose() {
  if (closing) return;
  closing = true;
  try {
    const state = await api('/api/status');
    if (!state.available) throw new Error(state.error || 'Sandbox status is unavailable.');
    if (state.activeJob) {
      const answer = await dialog.showMessageBox(win, {
        type: 'question', title: 'A reply is running', message: 'Stop the sandbox and close AICage?',
        detail: 'Closing now will stop the agent and its running commands. Workspace files are kept.',
        buttons: ['Keep working', 'Stop sandbox and close'], defaultId: 0, cancelId: 0, noLink: true
      });
      if (answer.response !== 1) return;
      await api('/api/control', { action: 'stop' });
    }
    saveBounds(); allowClose = true; win.close();
  } catch (error) {
    await dialog.showMessageBox(win, { type: 'error', title: 'Could not close safely',
      message: 'The sandbox state could not be confirmed.',
      detail: `${error.message}\nUse Stop sandbox, then close the window again.` });
  } finally { closing = false; }
}
function cleanup() {
  quitting = true;
  if (helper && helper.exitCode === null) helper.kill();
}
function failure(message) {
  if (quitting || failed) return;
  failed = true;
  dialog.showErrorBox('AICage could not start', message.replace(/#token=[^\s]+/g, '#token=[hidden]'));
  allowClose = true; cleanup(); app.exit(1);
}
async function startHelper() {
  const config = configuration();
  helper = spawn(config.server, ['--controller', config.controller, '--assets', config.assets,
    '--port', String(port), '--parent-pid', String(process.pid)], {
    windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], shell: false
  });
  let errors = '';
  helper.stderr.on('data', bytes => { errors = (errors + bytes.toString()).slice(-4000); });
  helper.once('exit', () => { if (helperReady && !quitting) failure('The local desktop helper exited. Reopen AICage to reconnect. The sandbox may still be running.'); });
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('The desktop helper did not start in time.')), 15000);
    const finish = (error, url) => { clearTimeout(timer); if (error) reject(error); else resolve(url); };
    helper.once('error', error => finish(error));
    helper.once('exit', code => { if (!helperReady) finish(new Error(`Desktop helper exited (${code}). ${errors}`)); });
    let output = '';
    helper.stdout.on('data', bytes => {
      if (helperReady) return;
      output += bytes.toString();
      if (output.length > 4096) return finish(new Error('Invalid desktop helper response.'));
      if (!output.includes('\n')) return;
      try {
        const url = new URL(output.split('\n')[0].trim());
        const candidate = new URLSearchParams(url.hash.slice(1)).get('token');
        if (!localPage(url.href) || !/^[a-f0-9]{64}$/.test(candidate || '')) throw new Error('Invalid desktop session.');
        token = candidate; helperReady = true; finish(null, url.href);
      } catch (error) { finish(error); }
    });
  });
}
async function createWindow(url) {
  const ses = session.defaultSession;
  await ses.setProxy({ mode: 'direct' });
  const clipboardWrite = (contents, permission) => permission === 'clipboard-sanitized-write' && contents && localPage(contents.getURL());
  ses.setPermissionCheckHandler((contents, permission) => Boolean(clipboardWrite(contents, permission)));
  ses.setPermissionRequestHandler((contents, permission, callback) => callback(Boolean(clipboardWrite(contents, permission))));
  ses.setDevicePermissionHandler(() => false);
  ses.setDisplayMediaRequestHandler((_request, callback) => callback({}));
  ses.on('will-download', event => event.preventDefault());
  // The renderer only needs our local static files and fixed authenticated APIs.
  // Public web requests belong to the isolated agent, not the desktop renderer.
  ses.webRequest.onBeforeRequest((details, callback) => {
    let allowed = false;
    try { const request = new URL(details.url); allowed = request.origin === origin && !request.username && !request.password; } catch {}
    callback({ cancel: !allowed });
  });
  const position = bounds();
  win = new BrowserWindow({ ...position, minWidth: 800, minHeight: 560, show: false, title: 'AICage',
    backgroundColor: '#ffffff', autoHideMenuBar: true,
    webPreferences: { nodeIntegration: false, nodeIntegrationInWorker: false, nodeIntegrationInSubFrames: false,
      contextIsolation: true, sandbox: true, webSecurity: true, webviewTag: false,
      allowRunningInsecureContent: false, navigateOnDragDrop: false, spellcheck: true }
  });
  Menu.setApplicationMenu(Menu.buildFromTemplate([
    { label: 'App', submenu: [{ label: 'Close window', accelerator: 'Alt+F4', click: () => win.close() }] },
    { label: 'Edit', submenu: [{ role: 'undo' }, { role: 'redo' }, { type: 'separator' }, { role: 'cut' }, { role: 'copy' }, { role: 'paste' }, { role: 'selectAll' }] },
    { label: 'View', submenu: [{ role: 'resetZoom' }, { role: 'zoomIn' }, { role: 'zoomOut' }, { type: 'separator' }, { role: 'togglefullscreen' }] }
  ]));
  win.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  win.webContents.on('will-navigate', (event, destination) => { if (!localPage(destination)) event.preventDefault(); });
  win.webContents.on('will-redirect', (event, destination) => { if (!localPage(destination)) event.preventDefault(); });
  win.webContents.on('will-attach-webview', event => event.preventDefault());
  win.webContents.on('will-prevent-unload', event => {
    // The frontend's browser-tab guard can still see a stale active turn after
    // the controller has confirmed stop. Only the verified close path wins.
    if (allowClose) event.preventDefault();
  });
  win.webContents.on('page-title-updated', event => { event.preventDefault(); win.setTitle('AICage'); });
  win.webContents.on('context-menu', (_event, params) => {
    const items = params.isEditable ? [{ role: 'cut' }, { role: 'copy' }, { role: 'paste' }, { role: 'selectAll' }]
      : params.selectionText ? [{ role: 'copy' }] : [];
    if (items.length) Menu.buildFromTemplate(items).popup({ window: win });
  });
  win.on('close', event => { if (!allowClose) { event.preventDefault(); void requestClose(); } });
  win.on('closed', cleanup);
  win.once('ready-to-show', () => { if (position.maximized) win.maximize(); win.show(); win.focus(); });
  await win.loadURL(url);
}
if (!ownsLock) {
  app.quit();
} else {
  app.on('second-instance', () => { if (win) { if (win.isMinimized()) win.restore(); win.show(); win.focus(); } });
  app.on('window-all-closed', () => app.quit());
  app.on('will-quit', cleanup);
  process.on('exit', cleanup);
  app.whenReady().then(async () => createWindow(await startHelper())).catch(error => failure(error.message));
}
