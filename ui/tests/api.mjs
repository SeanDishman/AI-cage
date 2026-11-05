import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import { fixture, finish, delay } from './harness.mjs';
const server = await fixture(43137); let passed = 0;
function pass(name) { passed++; console.log(`PASS ${name}`); }
try {
  let response = await fetch(server.origin + '/api/status'); assert.equal(response.status, 401);
  response = await server.call('/api/status', undefined, { Origin: 'https://untrusted.example' }); assert.equal(response.status, 403);
  response = await server.call('/api/status', undefined, { 'Sec-Fetch-Site': 'cross-site' }); assert.equal(response.status, 403);
  const rebound = await new Promise(resolve => {
    http.get(server.origin + '/api/status', { headers: { Host: 'attacker.example:43137', Authorization: `Bearer ${server.token}` } }, res => { res.resume(); resolve(res.statusCode); });
  }); assert.equal(rebound, 403); pass('Session token, Origin, Fetch Metadata and Host checks');
  response = await fetch(server.origin + '/'); assert.equal(response.status, 200);
  assert.match(response.headers.get('Content-Security-Policy'), /frame-ancestors 'none'/);
  assert.equal(response.headers.get('Cache-Control'), 'no-store');
  assert.equal((await fetch(server.origin + '/src/server.cpp')).status, 404); pass('Only UI assets served; CSP and cache protection');
  response = await server.call('/api/chat', { message: 'hello' }); assert.equal(response.status, 400);
  await server.call('/api/control', { action: 'start' });
  response = await server.call('/api/chat', { message: 'hello' }); assert.equal(response.status, 400);
  await server.call('/api/control', { action: 'internet', enabled: true });
  await server.call('/api/control', { action: 'sensors', enabled: true });
  response = await server.call('/api/status'); assert.equal(response.data.running, true); assert.equal(response.data.sensors, true); pass('Stopped/offline preconditions and real controller toggles');
  const prompt = 'Unicode ✓ café 日本語; literal "quotes" \\ and $(echo no) & | --flags\nsecond line';
  response = await server.call('/api/chat', { message: prompt, model: 'gpt-6-luna' }); assert.equal(response.status, 202);
  let events = await finish(server, response.data.job);
  assert(events.some(e => e.type === 'message' && e.text.includes(prompt)));
  assert(events.some(e => e.type === 'activity' && e.status === 'completed'));
  let args = JSON.parse(await readFile(path.join(server.directory, 'arguments.json'), 'utf8'));
  assert.equal(args.at(-1), prompt); assert.equal(args.at(-2), '--'); assert(args.includes('--model'));
  const thread = events.find(e => e.type === 'thread').id; pass('Unicode and shell metacharacters survive as literal argv; JSON events arrive');
  response = await server.call('/api/chat', { message: 'Continue', thread, model: '' }); assert.equal(response.status, 202);
  await finish(server, response.data.job); args = JSON.parse(await readFile(path.join(server.directory, 'arguments.json'), 'utf8'));
  assert(args.includes('resume')); assert(args.includes(thread)); assert(!args.includes('--model')); pass('Follow-up resumes the sandbox thread and honors default model');
  response = await server.call('/api/chat', { message: 'test-error' });
  events = await finish(server, response.data.job); assert(events.some(e => e.type === 'error' && e.message.includes('Synthetic model access error'))); pass('Agent/model failures reach the UI');
  for (const body of [{ message: 'x', thread: '../bad' }, { message: 'x', model: '--host-command' }, { message: 'x'.repeat(12001) }, { message: 'x\u0000y' }]) {
    assert.equal((await server.call('/api/chat', body)).status, 400);
  }
  assert.equal((await server.call('/api/control', { action: 'run', command: 'cmd' })).status, 400);
  assert.equal((await server.call('/api/control', { action: 'internet', enabled: 'yes' })).status, 400); pass('Input limits and fixed action/model/thread allowlists');
  response = await server.call('/api/chat', { message: 'test-long' }); assert.equal(response.status, 202); const longJob = response.data.job;
  assert.equal((await server.call('/api/chat', { message: 'overlap' })).status, 400);
  await delay(100); const start = performance.now();
  response = await server.call('/api/control', { action: 'stop' }); assert.equal(response.status, 200); assert.equal(response.data.state.running, false);
  assert(performance.now() - start < 5000); events = await finish(server, longJob); assert(events.some(e => e.type === 'cancelled'));
  assert.equal((await server.call('/api/chat', { message: 'after stop' })).status, 400); pass('Emergency stop cancels a busy turn and prevents automatic restart');
  response = await server.call('/api/logs'); assert.equal(response.data.entries[0].type, 'fixture_event'); pass('Sensor feed is relayed from controller output');
  console.log(`${passed} API checks passed using an explicit synthetic controller fixture.`);
} finally { server.stop(); }
