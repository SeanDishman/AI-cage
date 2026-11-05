// Actual Electron windows, synthetic controller. Never toggles the real sandbox.
import { _electron as electron } from 'playwright';
import electronBinary from 'electron';
import assert from 'node:assert/strict';
import { mkdir, mkdtemp, writeFile, readFile } from 'node:fs/promises';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { ui, delay } from './harness.mjs';
const output = path.join(ui, 'test-results');
await mkdir(output, { recursive: true });
const directory = await mkdtemp(path.join(output, 'desktop-fixture-'));
const profile = path.join(directory, 'profile');
for (const name of ['running', 'internet', 'sensors']) await writeFile(path.join(directory, name), '1');
const env = { ...process.env, AICAGE_DESKTOP_TEST: '1', AICAGE_DESKTOP_TEST_DATA: profile,
  AICAGE_DESKTOP_TEST_PORT: '43148', AICAGE_DESKTOP_TEST_CONTROLLER: path.join(ui, 'build', 'ui-controller-fixture.exe'),
  AICAGE_UI_FIXTURE_DIR: directory };
delete env.ELECTRON_RUN_AS_NODE;
let desktop;
const checks = [];
function pass(label) { checks.push(label); console.log(`PASS ${label}`); }
async function launch() {
  desktop = await electron.launch({ executablePath: electronBinary, args: [path.join(ui, 'desktop', 'app')], env, timeout: 30000 });
  const page = await desktop.firstWindow();
  await page.waitForFunction(() => document.getElementById('connection-text')?.textContent === 'Running', null, { timeout: 30000 });
  return page;
}
async function helperGone() {
  for (let i = 0; i < 100; i++) {
    try { await fetch('http://127.0.0.1:43148/', { signal: AbortSignal.timeout(500) }); }
    catch { return; }
    await delay(100);
  }
  throw new Error('Desktop helper did not exit with the window');
}
try {
  let page = await launch();
  if (!process.argv.includes('--crash-only')) {
  const preferences = await desktop.evaluate(({ BrowserWindow }) => {
    const w = BrowserWindow.getAllWindows()[0], p = w.webContents.getLastWebPreferences();
    return { title: w.getTitle(), sandbox: p.sandbox, isolation: p.contextIsolation, node: p.nodeIntegration, webview: p.webviewTag, webSecurity: p.webSecurity };
  });
  assert.deepEqual(preferences, { title: 'AICage', sandbox: true, isolation: true, node: false, webview: false, webSecurity: true });
  assert.deepEqual(await page.evaluate(() => [typeof process, typeof require, typeof window.electron]), ['undefined', 'undefined', 'undefined']);
  assert(!page.url().includes('token='));
  pass('Native Electron window; renderer sandbox, context isolation and no Node or host bridge');
  await page.locator('#prompt').fill('Desktop fixture hello'); await page.locator('#prompt').press('Enter');
  await page.waitForSelector('.assistant .message-body');
  await page.waitForFunction(() => document.getElementById('thinking').hidden);
  assert.match(await page.locator('.assistant').innerText(), /Synthetic fixture reply/);
  await page.locator('#prompt').fill('Desktop follow-up'); await page.locator('#prompt').press('Enter');
  await page.waitForFunction(() => document.querySelectorAll('.assistant').length === 2 && document.getElementById('thinking').hidden);
  assert((JSON.parse(await readFile(path.join(directory, 'arguments.json'), 'utf8'))).includes('resume'));
  pass('Electron chat, activity and conversation resume use the C++ controller');
  await desktop.evaluate(({ BrowserWindow }) => { BrowserWindow.getAllWindows()[0].setSize(1440, 940); });
  await page.locator('#sensors').click();
  await page.waitForFunction(() => document.getElementById('sensors').getAttribute('aria-checked') === 'false');
  await page.locator('#sensors').click();
  await page.waitForFunction(() => document.getElementById('sensors').getAttribute('aria-checked') === 'true');
  pass('Desktop sensor controls reflect confirmed controller state');
  const url = page.url();
  await page.evaluate(() => { window.open('https://example.com/'); const a = document.createElement('a'); a.href = 'https://example.com/'; document.body.append(a); a.click(); a.remove(); });
  await delay(250);
  assert.equal(page.url(), url); assert.equal(desktop.windows().length, 1);
  await page.evaluate(() => { location.href = 'file:///C:/Windows/win.ini'; });
  await delay(250); assert.equal(page.url(), url);
  const permissions = await page.evaluate(async () => Promise.all(['geolocation', 'camera', 'microphone', 'notifications'].map(async name => (await navigator.permissions.query({ name })).state)));
  assert(permissions.every(value => value === 'denied'));
  pass('External navigation, popup windows, file navigation and device permissions blocked');
  // Chromium cancels the forbidden navigation; reload also clears Playwright's
  // pending-navigation bookkeeping before the remaining interaction checks.
  await page.reload();
  await page.waitForFunction(() => document.getElementById('connection-text')?.textContent === 'Running');
  const second = spawn(electronBinary, [path.join(ui, 'desktop', 'app')], { env, windowsHide: true, stdio: 'ignore' });
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => { second.kill(); reject(new Error('Duplicate desktop did not exit')); }, 15000);
    second.once('error', reject); second.once('exit', code => { clearTimeout(timer); code === 0 ? resolve() : reject(new Error(`Duplicate exited ${code}`)); });
  });
  assert.equal(desktop.windows().length, 1);
  pass('Launching again focuses the existing desktop instead of starting a second server');
  await page.screenshot({ path: path.join(output, 'desktop-electron.png') });
  await desktop.evaluate(({ BrowserWindow }) => { BrowserWindow.getAllWindows()[0].setSize(900, 660); });
  await page.locator('#open-controls').click();
  assert(await page.locator('#emergency-stop').isVisible());
  assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
  await page.screenshot({ path: path.join(output, 'desktop-electron-compact.png') });
  await page.keyboard.press('Escape');
  pass('Full and compact desktop layouts keep chat and controls usable');
  await desktop.close(); desktop = undefined; await helperGone();
  page = await launch();
  assert.equal(await page.locator('.assistant').count(), 2);
  const savedBounds = await desktop.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows()[0].getSize());
  assert.deepEqual(savedBounds, [900, 660]);
  pass('App history and window size survive closing and reopening; helper exits');
  await page.locator('#prompt').fill('test-long'); await page.locator('#prompt').press('Enter');
  await page.waitForSelector('.activity summary');
  await page.waitForFunction(() => !document.getElementById('thinking').hidden);
  await desktop.evaluate(({ dialog, BrowserWindow }) => {
    globalThis.closeQuestions = 0;
    dialog.showMessageBox = async () => { globalThis.closeQuestions++; return { response: 0 }; };
    BrowserWindow.getAllWindows()[0].close();
  });
  for (let i = 0; i < 100 && !(await desktop.evaluate(() => globalThis.closeQuestions)); i++) await delay(100);
  assert.equal(await desktop.evaluate(() => globalThis.closeQuestions), 1);
  assert.equal(desktop.windows().length, 1);
  await desktop.evaluate(({ dialog, BrowserWindow }) => {
    globalThis.closeDialogs = [];
    dialog.showMessageBox = async (_owner, options) => { globalThis.closeDialogs.push(options); return { response: 1 }; };
    BrowserWindow.getAllWindows()[0].close();
  });
  try { await helperGone(); }
  catch (error) { throw new Error(`${error.message}: ${JSON.stringify(await desktop.evaluate(() => globalThis.closeDialogs))}`); }
  await desktop.close().catch(() => {}); desktop = undefined;
  let running = true; try { await readFile(path.join(directory, 'running')); } catch { running = false; }
  assert.equal(running, false);
  pass('Close during a turn offers keep-working or confirmed sandbox stop');
  await writeFile(path.join(directory, 'running'), '1');
  page = await launch();
  }
  const ownerPid = await desktop.evaluate(() => process.pid);
  // Playwright's Windows launcher PID can differ from Electron's main process.
  // Terminate the actual helper owner to exercise the native parent watcher.
  process.kill(ownerPid, 'SIGKILL');
  await helperGone(); desktop = undefined;
  pass('C++ helper exits after an abrupt Electron crash');
  await writeFile(path.join(output, 'desktop-results.json'), JSON.stringify({ passed: true, checkedAt: new Date().toISOString(), checks }, null, 2));
  console.log(`${checks.length} desktop integration checks passed.`);
} finally { if (desktop) await desktop.close().catch(() => desktop.process().kill()); }
