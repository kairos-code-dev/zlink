const assert = require('node:assert/strict');
const { fork } = require('node:child_process');
const net = require('node:net');
const path = require('node:path');
const test = require('node:test');
const { createClient } = require('redis');
const {
  ZLinkRedisLocationStore
} = require('@zlink-systems/framework-locations-redis');

const fixture = path.join(
  __dirname,
  'fixtures',
  'user-spot-native-process.js'
);
const redisUrl = process.env.ZLINK_TEST_REDIS_URL ?? 'redis://127.0.0.1:6379';

test(
  'public SpotManager completes command 47/48 through two native MeshNode processes',
  { timeout: 30_000 },
  async (t) => {
    const runId = `${process.pid}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
    const meshName = `m6b-native-${runId}`;
    const keyPrefix = `zlink:test:m6b-node:${runId}:`;
    const targetRoutingId = `m6b-target-${runId}`;
    const [sourcePort, targetPort] = await Promise.all([
      reservePort(),
      reservePort()
    ]);
    const children = [];
    let descriptorStore;
    const redis = createClient({
      url: redisUrl,
      socket: { reconnectStrategy: false }
    });
    redis.on('error', () => {});
    try {
      await redis.connect();
      await redis.ping();
      descriptorStore = new ZLinkRedisLocationStore({
        client: redis,
        keyPrefix
      });
    } catch (error) {
      if (redis.isOpen) redis.disconnect();
      t.skip(`Redis is unavailable at ${redisUrl}: ${error.message}`);
      return;
    }
    t.after(async () => {
      await Promise.all(children.map(stopChild));
      await descriptorStore?.dispose();
      await deletePrefix(redis, keyPrefix);
      if (redis.isOpen) await redis.quit();
    });

    const target = startChild(children, {
      role: 'target',
      meshName,
      endpoint: `tcp://127.0.0.1:${targetPort}`,
      routingId: targetRoutingId,
      targetRoutingId,
      keyPrefix
    });
    const targetReady = await target.ready;
    assert.equal(targetReady.role, 'target');

    const source = startChild(children, {
      role: 'source',
      meshName,
      endpoint: `tcp://127.0.0.1:${sourcePort}`,
      routingId: `m6b-source-${runId}`,
      targetRoutingId,
      keyPrefix
    });
    const sourceReady = await source.ready;
    assert.equal(sourceReady.role, 'source');
    assert.notEqual(sourceReady.pid, targetReady.pid);
    const targetDescriptor = (await descriptorStore.listMeshNodes(meshName))
      .find((descriptor) => String(descriptor.rid) === targetRoutingId);
    assert.ok(targetDescriptor);
    assert.equal(targetDescriptor.descriptorRevision, 2n);
    assert.equal(targetDescriptor.state, 1);
    assert.equal(targetDescriptor.endpoint, `tcp://127.0.0.1:${targetPort}`);
    assert.equal(targetDescriptor.objectRole, 'server');
    assert.ok(targetDescriptor.objectCapabilities.some((capability) =>
      capability.objectKind === 'user_spot'
      && capability.stableType === 'RemoteRoom'
    ));

    const created = await source.command('create');
    assert.equal(created.state, 'created');
    assert.equal(created.spot.meshName, meshName);
    assert.equal(created.spot.nodeRid, targetRoutingId);

    const existing = await source.command('getOrCreate');
    assert.equal(existing.state, 'existing');
    assert.deepEqual(existing.spot, created.spot);

    assert.deepEqual(await source.command('find'), created.spot);
    assert.equal(await source.command('close'), true);
    assert.equal(await source.command('findAfterClose'), null);
  }
);

function startChild(children, options) {
  const child = fork(fixture, [], {
    cwd: path.resolve(__dirname, '../..'),
    env: {
      ...process.env,
      ZLINK_TEST_ROLE: options.role,
      ZLINK_TEST_MESH: options.meshName,
      ZLINK_TEST_ENDPOINT: options.endpoint,
      ZLINK_TEST_ROUTING_ID: options.routingId,
      ZLINK_TEST_TARGET_ROUTING_ID: options.targetRoutingId,
      ZLINK_TEST_REDIS_URL: redisUrl,
      ZLINK_TEST_REDIS_PREFIX: options.keyPrefix
    },
    stdio: ['ignore', 'pipe', 'pipe', 'ipc']
  });
  children.push(child);
  let stderr = '';
  child.stderr.setEncoding('utf8');
  child.stderr.on('data', (chunk) => {
    stderr += chunk;
  });
  let nextId = 1;
  const pending = new Map();
  const ready = new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      reject(new Error(`${options.role} readiness timed out.\n${stderr}`));
    }, 10_000);
    const onMessage = (message) => {
      if (message?.type === 'ready') {
        clearTimeout(timeout);
        resolve(message);
      } else if (message?.type === 'fatal') {
        clearTimeout(timeout);
        reject(new Error(message.message));
      }
    };
    child.on('message', onMessage);
    child.once('exit', (code, signal) => {
      clearTimeout(timeout);
      reject(new Error(
        `${options.role} exited before ready: code=${code} signal=${signal}\n${stderr}`
      ));
    });
  });
  child.on('message', (message) => {
    if (
      (message?.type !== 'result' && message?.type !== 'error')
      || !pending.has(message.id)
    ) {
      return;
    }
    const operation = pending.get(message.id);
    pending.delete(message.id);
    if (message.type === 'error') {
      operation.reject(new Error(message.message));
    } else {
      operation.resolve(message.value);
    }
  });
  child.once('exit', (code, signal) => {
    const failure = new Error(
      `${options.role} exited: code=${code} signal=${signal}\n${stderr}`
    );
    for (const operation of pending.values()) operation.reject(failure);
    pending.clear();
  });
  return {
    child,
    ready,
    command(action) {
      const id = nextId++;
      return new Promise((resolve, reject) => {
        const timeout = setTimeout(() => {
          pending.delete(id);
          reject(new Error(`${options.role} command '${action}' timed out.`));
        }, 10_000);
        pending.set(id, {
          resolve(value) {
            clearTimeout(timeout);
            resolve(value);
          },
          reject(error) {
            clearTimeout(timeout);
            reject(error);
          }
        });
        child.send({ id, action }, (error) => {
          if (error === null) return;
          const operation = pending.get(id);
          pending.delete(id);
          operation?.reject(error);
        });
      });
    }
  };
}

async function stopChild(entry) {
  const child = entry.child ?? entry;
  if (child.exitCode !== null || child.signalCode !== null) return;
  const exited = new Promise((resolve) => child.once('exit', resolve));
  if (child.connected) child.send({ type: 'stop' }, () => {});
  else child.kill('SIGTERM');
  const timer = setTimeout(() => child.kill('SIGKILL'), 5_000);
  await exited;
  clearTimeout(timer);
}

async function reservePort() {
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const { port } = server.address();
  await new Promise((resolve, reject) => {
    server.close((error) => error === undefined ? resolve() : reject(error));
  });
  return port;
}

async function deletePrefix(redis, prefix) {
  const keys = [];
  for await (const key of redis.scanIterator({
    MATCH: `${prefix}*`,
    COUNT: 100
  })) {
    keys.push(...(Array.isArray(key) ? key : [key]));
  }
  if (keys.length > 0) await redis.del(keys);
}
