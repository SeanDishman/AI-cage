import { spawn } from 'node:child_process';
import { mkdir, mkdtemp } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
export const ui = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
export const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
export async function fixture(port) {
  const results = path.join(ui, 'test-results'); await mkdir(results, { recursive: true });
  const directory = await mkdtemp(path.join(results, 'fixture-'));
  const child = spawn(path.join(ui, 'build', 'AICageUI.exe'), [
    '--port', String(port), '--controller', path.join(ui, 'build', 'ui-controller-fixture.exe'), '--assets', path.join(ui, 'web')
  ], { windowsHide: true, env: { ...process.env, AICAGE_UI_FIXTURE_DIR: directory }, stdio: ['ignore', 'pipe', 'pipe'] });
  let output = '', errors = '';
  child.stderr.on('data', data => errors += data);
  const url = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => { child.kill(); reject(new Error(`UI server startup timed out: ${errors}`)); }, 10000);
    child.on('error', error => { clearTimeout(timer); reject(error); });
    child.on('exit', code => { clearTimeout(timer); reject(new Error(`UI server exited (${code}): ${errors}`)); });
    child.stdout.on('data', data => { output += data; if (output.includes('\n')) { clearTimeout(timer); resolve(output.split('\n')[0].trim()); } });
  });
  const parsed = new URL(url), token = new URLSearchParams(parsed.hash.slice(1)).get('token'), origin = parsed.origin;
  const call = async (route, body, extra = {}) => {
    const response = await fetch(origin + route, { method: body === undefined ? 'GET' : 'POST',
      headers: { Authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'Content-Type': 'application/json' }), ...extra },
      ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
    return { status: response.status, data: await response.json(), headers: response.headers };
  };
  return { child, directory, url, origin, token, call, stop: () => child.kill() };
}
export async function finish(server, job) {
  let after = 0, events = [];
  for (let i = 0; i < 120; i++) {
    const response = await server.call(`/api/events?job=${job}&after=${after}`);
    if (response.status !== 200) throw new Error(JSON.stringify(response.data));
    events.push(...response.data.events); if (events.length) after = events.at(-1).seq;
    if (response.data.done) return events;
    await delay(50);
  }
  throw new Error('Job did not finish');
}
