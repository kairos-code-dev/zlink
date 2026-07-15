const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const { ZLinkRedisLocationStore } = require('../../packages/framework-locations-redis/dist');

const nodeRoot = path.resolve(__dirname, '../..');
const sampleRoot = path.join(nodeRoot, 'samples/ZoneWorld');

test('ZoneWorld allocates one grouped routing id and hands a bounded slot to a replacement', async (t) => {
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'zlink-zoneworld-routing-'));
  const keyPrefix = `zoneworld:node:contract:${process.pid}:${Date.now()}:`;
  const processes = [];
  const store = new ZLinkRedisLocationStore({
    url: 'redis://127.0.0.1:6379',
    keyPrefix: `${keyPrefix}location`
  });
  t.after(async () => {
    for (const process of processes.reverse()) await stop(process);
    await store.dispose();
    fs.rmSync(temp, { recursive: true, force: true });
  });

  const east = await configuration(temp, 'east', 'zone-node-2', keyPrefix);
  const west = await configuration(temp, 'west', 'zone-node-1', keyPrefix);
  const replacement = await configuration(temp, 'replacement', 'zone-node-2', keyPrefix);

  const eastProcess = start(east.path);
  processes.push(eastProcess);
  const eastReady = await eastProcess.waitFor('zoneworld routing allocation ready');
  assert.match(eastReady, /slot=1/);
  assertSharedMembers(eastReady, 'zn1');

  const westProcess = start(west.path);
  processes.push(westProcess);
  const westReady = await westProcess.waitFor('zoneworld routing allocation ready');
  assert.match(westReady, /slot=2/);
  assertSharedMembers(westReady, 'zn2');

  const before = await store.listRoutingIdSlots('zoneworld.zone-node');
  const oldGeneration = generationAt(before, 1);
  const replacementProcess = start(replacement.path);
  processes.push(replacementProcess);
  await delay(500);
  assert.equal(await canConnect(replacement.value.zoneNode.spotRouterEndpoint), false);
  assert.doesNotMatch(replacementProcess.output(), /zoneworld routing allocation ready/);

  await stop(eastProcess);
  const replacementReady = await replacementProcess.waitFor('zoneworld routing allocation ready');
  assert.match(replacementReady, /slot=1/);
  assertSharedMembers(replacementReady, 'zn1');
  const after = await store.listRoutingIdSlots('zoneworld.zone-node');
  assert.ok(generationAt(after, 1) > oldGeneration);
});

function assertSharedMembers(output, routingId) {
  assert.match(output, new RegExp(`zoneworld\\.zones=${routingId}`));
  assert.match(output, new RegExp(`zoneworld\\.bridge=${routingId}`));
  assert.match(output, new RegExp(`zoneworld\\.report=${routingId}`));
}

async function configuration(directory, name, nodeId, redisKeyPrefix) {
  const value = {
    shared: { redisEndpoint: '127.0.0.1:6379', redisKeyPrefix, logDirectory: directory },
    zoneNode: {
      nodeId,
      spotRouterEndpoint: `tcp://127.0.0.1:${await freePort()}`,
      spotPubSubEndpoint: `tcp://127.0.0.1:${await freePort()}`,
      opsChannelEndpoint: `tcp://127.0.0.1:${await freePort()}`,
      actorsChannelEndpoint: `tcp://127.0.0.1:${await freePort()}`,
      bridgeEndpoint: `tcp://127.0.0.1:${await freePort()}`
    }
  };
  const configPath = path.join(directory, `${name}.json`);
  fs.writeFileSync(configPath, JSON.stringify({ sample: value }));
  return { path: configPath, value };
}

function start(configPath) {
  const child = spawn(process.execPath, ['dist/Server/ZoneNode/main.js', '--config', configPath], {
    cwd: sampleRoot,
    env: process.env,
    stdio: ['ignore', 'pipe', 'pipe']
  });
  let output = '';
  child.stdout.on('data', (chunk) => { output += chunk; });
  child.stderr.on('data', (chunk) => { output += chunk; });
  return {
    child,
    output: () => output,
    waitFor: (text) => waitForOutput(child, () => output, text)
  };
}

function waitForOutput(child, output, expected) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error(`Timed out waiting for '${expected}'.\n${output()}`)), 15_000);
    const poll = setInterval(() => {
      if (output().includes(expected)) {
        clearTimeout(timeout);
        clearInterval(poll);
        resolve(output());
      }
    }, 20);
    child.once('exit', (code) => {
      if (!output().includes(expected)) {
        clearTimeout(timeout);
        clearInterval(poll);
        reject(new Error(`ZoneWorld process exited ${code} before '${expected}'.\n${output()}`));
      }
    });
  });
}

async function stop(process) {
  if (process.child.exitCode !== null) return;
  process.child.kill('SIGTERM');
  await Promise.race([
    new Promise((resolve) => process.child.once('exit', resolve)),
    delay(5_000).then(() => process.child.kill('SIGKILL'))
  ]);
}

function generationAt(snapshot, slot) {
  const allocation = snapshot.allocations.find((candidate) => candidate.slot === slot);
  assert.ok(allocation, `Routing-id slot ${slot} must be allocated.`);
  return allocation.owner.generation;
}

function freePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const address = server.address();
      assert.ok(address && typeof address !== 'string');
      const port = address.port;
      server.close((error) => error ? reject(error) : resolve(port));
    });
  });
}

function canConnect(endpoint) {
  const url = new URL(endpoint.replace(/^tcp:/, 'http:'));
  return new Promise((resolve) => {
    const socket = net.connect(Number(url.port), url.hostname);
    socket.once('connect', () => { socket.destroy(); resolve(true); });
    socket.once('error', () => resolve(false));
    socket.setTimeout(250, () => { socket.destroy(); resolve(false); });
  });
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}
