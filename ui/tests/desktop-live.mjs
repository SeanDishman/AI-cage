// Opt-in packaged Electron check. One tiny real model turn; leaves sandbox running.
import { _electron as electron } from 'playwright';
import assert from 'node:assert/strict';
import { writeFile } from 'node:fs/promises';
import path from 'node:path';
import { ui } from './harness.mjs';
const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
const desktop = await electron.launch({ executablePath: path.join(ui, 'dist', 'AICage-win32-x64', 'AICage Desktop.exe'), env, timeout: 30000 });
try {
  const page = await desktop.firstWindow();
  await page.waitForFunction(() => document.getElementById('connection-text')?.textContent === 'Running', null, { timeout: 60000 });
  await page.locator('#new-chat').click();
  await page.locator('#prompt').fill('Reply with exactly ELECTRON_READY. Do not run commands, use tools, or modify files.');
  await page.locator('#prompt').press('Enter');
  await page.waitForFunction(() => document.querySelector('.assistant') || document.querySelector('.error-message'), null, { timeout: 240000 });
  if (await page.locator('.error-message').count()) throw new Error(await page.locator('.error-message').first().innerText());
  const reply = await page.locator('.assistant .message-body').innerText(); assert.match(reply, /ELECTRON_READY/);
  await page.waitForFunction(() => document.getElementById('thinking').hidden);
  await page.screenshot({ path: path.join(ui, 'test-results', 'desktop-live.png') });
  await writeFile(path.join(ui, 'test-results', 'desktop-live.json'), JSON.stringify({ passed: true, checkedAt: new Date().toISOString(), reply, packaged: await desktop.evaluate(({ app }) => app.isPackaged) }, null, 2));
  // Leave the next normal launch on a clean conversation for the user.
  await page.locator('#new-chat').click();
  console.log('PASS packaged Electron window: real GPT-6 Luna reply through sandbox.');
} finally { await desktop.close(); }
