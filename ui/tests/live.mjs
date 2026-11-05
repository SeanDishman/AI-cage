// Opt-in live smoke check: uses the configured sandbox account for two tiny turns.
// No network toggle, stop, setup, auth import, or workspace mutation is requested.
import { chromium } from 'playwright';
import { spawn } from 'node:child_process';
import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import { ui } from './harness.mjs';
const child = spawn(path.join(ui, 'build', 'AICageUI.exe'), ['--port', '43139'], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
let errors = ''; child.stderr.on('data', data => errors += data);
const url = await new Promise((resolve, reject) => {
  let output = ''; const timeout = setTimeout(() => { child.kill(); reject(new Error(`UI startup timeout: ${errors}`)); }, 10000);
  child.on('error', reject); child.on('exit', code => { clearTimeout(timeout); reject(new Error(`UI exited ${code}: ${errors}`)); });
  child.stdout.on('data', data => { output += data; if (output.includes('\n')) { clearTimeout(timeout); resolve(output.split('\n')[0].trim()); } });
});
const browser = await chromium.launch({ channel: 'chrome', headless: true });
const result = { test: 'live sandbox UI', model: 'gpt-6-luna', checkedAt: new Date().toISOString(), passed: false };
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 960 }, colorScheme: 'light', reducedMotion: 'reduce' });
  await page.goto(url); await page.waitForFunction(() => document.getElementById('connection-text').textContent === 'Running', { timeout: 30000 });
  assert.equal(await page.locator('#internet').getAttribute('aria-checked'), 'true');
  console.log('Live UI connected to running main sandbox; starting GPT-6 Luna turn.');
  await page.locator('#prompt').fill('Reply with exactly UI_LUNA_READY. Do not run commands, use tools, or modify files.');
  await page.locator('#prompt').press('Enter');
  await page.waitForFunction(() => document.querySelector('.assistant') || document.querySelector('.error-message'), null, { timeout: 240000 });
  if (await page.locator('.error-message').count()) throw new Error(await page.locator('.error-message').first().innerText());
  result.firstReply = await page.locator('.assistant .message-body').innerText(); assert.match(result.firstReply, /UI_LUNA_READY/);
  await page.waitForFunction(() => document.getElementById('thinking').hidden);
  console.log('GPT-6 Luna replied through the UI; checking conversation resume.');
  await page.locator('#prompt').fill('What exact marker did I ask you to say in my previous message? Reply only with that marker. Do not use tools.');
  await page.locator('#prompt').press('Enter');
  await page.waitForFunction(() => document.querySelectorAll('.assistant').length === 2 || document.querySelector('.error-message'), null, { timeout: 240000 });
  if (await page.locator('.error-message').count()) throw new Error(await page.locator('.error-message').first().innerText());
  result.resumedReply = await page.locator('.assistant .message-body').last().innerText(); assert.match(result.resumedReply, /UI_LUNA_READY/);
  await page.waitForFunction(() => document.getElementById('thinking').hidden);
  result.passed = true;
  await page.screenshot({ path: path.join(ui, 'test-results', 'live-chat.png'), fullPage: true });
  console.log('PASS live GPT-6 Luna chat and remembered follow-up through AICageUI → sandbox controller → Codex.');
} catch (error) { result.error = error.message; console.error(`Live UI verification failed: ${error.message}`); process.exitCode = 1; }
finally {
  await mkdir(path.join(ui, 'test-results'), { recursive: true });
  await writeFile(path.join(ui, 'test-results', 'live.json'), JSON.stringify(result, null, 2));
  await browser.close(); child.kill();
}
