'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
const net = require('node:net');
const path = require('node:path');
const zlink = require('../dist');

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

test('socket monitor exposes recv and tryRecv with empty path', () => {
  const ctx = new zlink.Context();
  const socket = new zlink.PairSocket(ctx);
  const monitor = socket.monitorOpen(zlink.MonitorEvent.ALL);

  assert.equal(monitor.tryRecv(), null);

  monitor.close();
  socket.close();
  ctx.close();
});

test('socket monitor receives bind state events', async () => {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = new zlink.Context();
  const socket = new zlink.PairSocket(ctx);
  const monitor = socket.monitorOpen(zlink.MonitorEvent.ALL);
  let client;

  try {
    socket.bind(endpoint);
    client = net.createConnection({ host: '127.0.0.1', port });
    await once(client, 'connect');

    const event = monitor.recv();
    assert.equal(event.event, zlink.MonitorEvent.LISTENING);
  } finally {
    if (client) {
      client.destroy();
    }
    monitor.close();
    socket.close();
    ctx.close();
  }
});

test('service monitors expose tryRecv empty path', () => {
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);
  const monitor = spot.monitorOpen();

  assert.equal(typeof monitor.recv, 'function');
  assert.equal(monitor.tryRecv(), null);

  monitor.close();
  spot.close();
  node.close();
  ctx.close();
});

test('spot service monitor onEvent delivers filter-applied callbacks', async () => {
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);
  const monitor = spot.monitorOpen(
    zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED
      | zlink.ServiceMonitorEvent.ERROR
  );

  try {
    const event = await new Promise((resolve, reject) => {
      const timeoutId = setTimeout(() => {
        reject(new Error('spot service monitor onEvent timeout'));
      }, 5000);
      monitor.onEvent((value) => {
        if (value.eventType === zlink.ServiceMonitorEvent.ERROR) {
          clearTimeout(timeoutId);
          reject(new Error(`service monitor error: ${JSON.stringify(value)}`));
          return;
        }
        if (value.eventType === zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED) {
          clearTimeout(timeoutId);
          resolve(value);
        }
      });
      spot.setSubscription('topic.monitor.callback');
    });

    assert.equal(event.eventType, zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED);
    assert.equal(event.subject, 'topic.monitor.callback');
  } finally {
    monitor.close();
    spot.close();
    node.close();
    ctx.close();
  }
});

test('discovery service monitor reports service-up events', async () => {
  const ctx = new zlink.Context();
  const registry = new zlink.Registry(ctx);
  const discovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'monitor-service-up');
  const node = new zlink.SpotNode(ctx);
  const pubEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const routerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const serviceEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const monitor = discovery.monitorOpen(
    zlink.ServiceMonitorEvent.DISCOVERY_SERVICE_UP
      | zlink.ServiceMonitorEvent.ERROR
  );

  try {
    registry.bind(pubEndpoint, routerEndpoint);
    discovery.connectRegistry(routerEndpoint);
    node.attachDiscovery(discovery);

    const eventPromise = new Promise((resolve, reject) => {
      const timeoutId = setTimeout(() => {
        reject(new Error('discovery service monitor timeout'));
      }, 5000);
      monitor.onEvent((event) => {
        if (event.eventType === zlink.ServiceMonitorEvent.ERROR) {
          clearTimeout(timeoutId);
          reject(new Error(`discovery monitor error: ${JSON.stringify(event)}`));
          return;
        }
        if (event.eventType === zlink.ServiceMonitorEvent.DISCOVERY_SERVICE_UP) {
          clearTimeout(timeoutId);
          resolve(event);
        }
      });
    });

    node.bind(serviceEndpoint);
    const event = await eventPromise;
    assert.equal(event.eventType, zlink.ServiceMonitorEvent.DISCOVERY_SERVICE_UP);
    assert.equal(event.serviceName, 'monitor-service-up');
  } finally {
    monitor.close();
    discovery.close();
    registry.close();
    ctx.close();
  }
});

test('spot service monitor reports remote sub delivery ready after direct peer connect', async () => {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = new zlink.Context();
  const serverNode = new zlink.SpotNode(ctx);
  const clientNode = new zlink.SpotNode(ctx);
  const serverSpot = new zlink.Spot(serverNode);
  const clientSpot = new zlink.Spot(clientNode);
  const monitor = clientSpot.monitorOpen(
    zlink.ServiceMonitorEvent.SPOT_SUB_DELIVERY_READY_CHANGED
      | zlink.ServiceMonitorEvent.ERROR
  );

  try {
    serverNode.bind(endpoint);
    clientNode.connectPeer(endpoint);
    clientSpot.setSubscription('topic.monitor.remote');

    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      const event = monitor.tryRecv();
      if (event) {
        assert.notEqual(event.eventType, zlink.ServiceMonitorEvent.ERROR);
        if (
          event.eventType === zlink.ServiceMonitorEvent.SPOT_SUB_DELIVERY_READY_CHANGED
          && event.value > 0
        ) {
          assert.equal(event.value, 1);
          assert.equal(clientNode.statusSnapshot().connectedPeerCount, 1);
          assert.equal(clientNode.statusSnapshot().readySubjectCount, 1);
          return;
        }
      }
      await new Promise((resolve) => setImmediate(resolve));
    }

    assert.fail(`spot remote delivery ready timeout: ${JSON.stringify({
      monitor: monitor.snapshot(),
      serverStatus: serverNode.statusSnapshot(),
      clientStatus: clientNode.statusSnapshot(),
      serverPeers: serverNode.peersSnapshot(),
      clientPeers: clientNode.peersSnapshot(),
      serverSubjects: serverNode.subjectsSnapshot(),
      clientSubjects: clientNode.subjectsSnapshot()
    })}`);
  } finally {
    monitor.close();
    clientSpot.close();
    serverSpot.close();
    clientNode.close();
    serverNode.close();
    ctx.close();
  }
});

test('spot service monitor does not report sub delivery ready before peer connect', async () => {
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);
  const monitor = spot.monitorOpen(
    zlink.ServiceMonitorEvent.SPOT_SUB_DELIVERY_READY_CHANGED
      | zlink.ServiceMonitorEvent.ERROR
  );

  try {
    spot.setSubscription('topic.monitor.local-only');

    const deadline = Date.now() + 500;
    while (Date.now() < deadline) {
      const event = monitor.tryRecv();
      if (!event) {
        await new Promise((resolve) => setImmediate(resolve));
        continue;
      }
      assert.notEqual(event.eventType, zlink.ServiceMonitorEvent.ERROR);
      assert.notEqual(event.eventType, zlink.ServiceMonitorEvent.SPOT_SUB_DELIVERY_READY_CHANGED);
    }
  } finally {
    monitor.close();
    spot.close();
    node.close();
    ctx.close();
  }
});

test('spot service monitor does not report remote pub delivery ready before peer connect', async () => {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = new zlink.Context();
  const serverNode = new zlink.SpotNode(ctx);
  const clientNode = new zlink.SpotNode(ctx);
  const serverSpot = new zlink.Spot(serverNode);
  const clientSpot = new zlink.Spot(clientNode);
  const monitor = serverSpot.monitorOpen(
    zlink.ServiceMonitorEvent.SPOT_PUB_DELIVERY_READY_CHANGED
      | zlink.ServiceMonitorEvent.SPOT_FIRST_DELIVERY_READY_CHANGED
      | zlink.ServiceMonitorEvent.ERROR
  );

  try {
    serverNode.bind(endpoint);

    const earlyDeadline = Date.now() + 200;
    while (Date.now() < earlyDeadline) {
      const event = monitor.tryRecv();
      if (!event) {
        await new Promise((resolve) => setImmediate(resolve));
        continue;
      }
      assert.notEqual(event.eventType, zlink.ServiceMonitorEvent.ERROR);
      assert.notEqual(event.eventType, zlink.ServiceMonitorEvent.SPOT_PUB_DELIVERY_READY_CHANGED);
      assert.notEqual(event.eventType, zlink.ServiceMonitorEvent.SPOT_FIRST_DELIVERY_READY_CHANGED);
    }

    clientNode.connectPeer(endpoint);
    clientSpot.setSubscription('topic.monitor.pub.remote');

    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      const event = monitor.tryRecv();
      if (event) {
        assert.notEqual(event.eventType, zlink.ServiceMonitorEvent.ERROR);
        if (
          (event.eventType === zlink.ServiceMonitorEvent.SPOT_PUB_DELIVERY_READY_CHANGED
            || event.eventType === zlink.ServiceMonitorEvent.SPOT_FIRST_DELIVERY_READY_CHANGED)
          && event.value > 0
        ) {
          assert.equal(event.value, 1);
          assert.equal(clientNode.statusSnapshot().connectedPeerCount, 1);
          assert.equal(clientNode.statusSnapshot().readySubjectCount, 1);
          return;
        }
      }
      await new Promise((resolve) => setImmediate(resolve));
    }

    assert.fail(`spot remote pub delivery ready timeout: ${JSON.stringify({
      monitor: monitor.snapshot(),
      serverStatus: serverNode.statusSnapshot(),
      clientStatus: clientNode.statusSnapshot(),
      serverPeers: serverNode.peersSnapshot(),
      clientPeers: clientNode.peersSnapshot(),
      serverSubjects: serverNode.subjectsSnapshot(),
      clientSubjects: clientNode.subjectsSnapshot()
    })}`);
  } finally {
    monitor.close();
    clientSpot.close();
    serverSpot.close();
    clientNode.close();
    serverNode.close();
    ctx.close();
  }
});

test('multi spot perf helper completes a single tcp recv run', async () => {
  const { spawnMultiPair } = require(path.join(
    __dirname,
    '..',
    'perf',
    'multi',
    'perf_multi_common.js'
  ));

  const resultLines = await spawnMultiPair(
    'perf_multi_spot_server.js',
    'perf_multi_spot_client.js',
    {
      pattern: 'MULTI_SPOT',
      recv: 'recv',
      duration: 1,
      warmup: 1,
      msgSize: 64,
      clients: 1
    }
  );

  assert.ok(resultLines.some((line) => (
    line.startsWith('RESULT,current,MULTI_SPOT,tcp,64,throughput,')
  )));
});

test('multi spot child processes complete a single tcp recv run on fixed endpoint', async () => {
  const serverPath = path.join(__dirname, '..', 'perf', 'multi', 'perf_multi_spot_server.js');
  const clientPath = path.join(__dirname, '..', 'perf', 'multi', 'perf_multi_spot_client.js');
  const endpoint = 'tcp://127.0.0.1:30123';
  const args = [
    '--endpoint', endpoint,
    '--msg-size', '64',
    '--warmup', '1',
    '--duration', '1',
    '--clients', '1',
    '--recv', 'recv'
  ];
  const resultLines = [];
  const childState = new WeakMap();

  const ensureCollector = (child) => {
    let state = childState.get(child);
    if (state) {
      return state;
    }

    state = { buffered: '', seen: [], waiters: [] };
    child.stdout.on('data', (chunk) => {
      state.buffered += chunk.toString();
      while (true) {
        const newline = state.buffered.indexOf('\n');
        if (newline === -1) {
          break;
        }
        const line = state.buffered.slice(0, newline).trim();
        state.buffered = state.buffered.slice(newline + 1);
        if (!line) {
          continue;
        }
        state.seen.push(line);
        if (line.startsWith('RESULT,')) {
          resultLines.push(line);
        }
        state.waiters = state.waiters.filter((waiter) => {
          if (waiter.expected !== line) {
            return true;
          }
          clearTimeout(waiter.timeout);
          waiter.resolve();
          return false;
        });
      }
    });
    child.once('exit', (code) => {
      const pending = state.waiters.slice();
      state.waiters.length = 0;
      for (const waiter of pending) {
        clearTimeout(waiter.timeout);
        waiter.reject(new Error(`exited before ${waiter.expected}: ${code}`));
      }
    });
    childState.set(child, state);
    return state;
  };

  const waitForLine = (child, expected, timeoutMs) => {
    return new Promise((resolve, reject) => {
      const state = ensureCollector(child);
      if (state.seen.includes(expected)) {
        resolve();
        return;
      }
      const waiter = {
        expected,
        resolve,
        reject,
        timeout: setTimeout(() => {
          state.waiters = state.waiters.filter((entry) => entry !== waiter);
          reject(new Error(`timeout waiting for ${expected}`));
        }, timeoutMs)
      };
      state.waiters.push(waiter);
    });
  };

  const server = spawn(process.execPath, [serverPath, ...args], {
    cwd: path.join(__dirname, '..', '..'),
    stdio: ['pipe', 'pipe', 'pipe']
  });
  const client = spawn(process.execPath, [clientPath, ...args], {
    cwd: path.join(__dirname, '..', '..'),
    stdio: ['ignore', 'pipe', 'pipe']
  });

  let serverStderr = '';
  let clientStderr = '';
  const waitForExit = (child) => {
    if (child.exitCode !== null || child.signalCode !== null) {
      return Promise.resolve();
    }
    return once(child, 'exit').then(() => {});
  };
  ensureCollector(server);
  ensureCollector(client);
  server.stderr.on('data', (chunk) => {
    serverStderr += chunk.toString();
  });
  client.stderr.on('data', (chunk) => {
    clientStderr += chunk.toString();
  });

  try {
    await waitForLine(server, `READY,${endpoint}`, 5000);
    await waitForLine(client, 'CLIENT_READY,64', 10000);
    await waitForLine(server, 'PEERS_READY,64', 10000);
    server.stdin.write('START,64\n');

    const [clientCode] = await once(client, 'exit');
    assert.equal(clientCode, 0, clientStderr);
    assert.ok(resultLines.some((line) => (
      line.startsWith('RESULT,current,MULTI_SPOT,tcp,64,throughput,')
    )));
  } finally {
    if (server.stdin.writable) {
      server.stdin.write('STOP\n');
      server.stdin.end();
    }
    server.kill('SIGTERM');
    client.kill('SIGTERM');
    await Promise.allSettled([waitForExit(server), waitForExit(client)]);
  }
});

test('spot sub delivery ready monitor works across child processes', async () => {
  const fixturesDir = path.join(__dirname, '..', '..', 'tests', 'fixtures');
  const serverPath = path.join(fixturesDir, 'spot_child_server.js');
  const clientPath = path.join(fixturesDir, 'spot_child_monitor_client.js');
  const endpoint = 'tcp://127.0.0.1:30123';
  const args = ['--endpoint', endpoint];

  const waitForLine = (child, expected, timeoutMs, sink) => {
    return new Promise((resolve, reject) => {
      let buffered = '';
      let done = false;
      const timeout = setTimeout(() => {
        if (!done) {
          done = true;
          reject(new Error(`timeout waiting for ${expected}: ${sink()}`));
        }
      }, timeoutMs);
      const onData = (chunk) => {
        buffered += chunk.toString();
        while (true) {
          const newline = buffered.indexOf('\n');
          if (newline === -1) {
            break;
          }
          const line = buffered.slice(0, newline).trim();
          buffered = buffered.slice(newline + 1);
          if (!line) {
            continue;
          }
          if (!done && line === expected) {
            done = true;
            clearTimeout(timeout);
            child.stdout.off('data', onData);
            resolve();
            return;
          }
        }
      };
      child.stdout.on('data', onData);
      child.once('exit', (code) => {
        if (!done) {
          done = true;
          clearTimeout(timeout);
          child.stdout.off('data', onData);
          reject(new Error(`exited before ${expected}: ${code}: ${sink()}`));
        }
      });
    });
  };

  const server = spawn(process.execPath, [serverPath, ...args], {
    cwd: path.join(__dirname, '..', '..'),
    stdio: ['pipe', 'pipe', 'pipe']
  });
  const client = spawn(process.execPath, [clientPath, ...args], {
    cwd: path.join(__dirname, '..', '..'),
    stdio: ['ignore', 'pipe', 'pipe']
  });

  let serverStderr = '';
  let clientStderr = '';
  server.stderr.on('data', (chunk) => {
    serverStderr += chunk.toString();
  });
  client.stderr.on('data', (chunk) => {
    clientStderr += chunk.toString();
  });

  try {
    await waitForLine(server, `READY,${endpoint}`, 5000, () => serverStderr);
    await waitForLine(client, 'CLIENT_READY', 5000, () => clientStderr);
    await waitForLine(client, 'EVENT_READY,1', 5000, () => clientStderr);
  } finally {
    if (server.stdin.writable) {
      server.stdin.end();
    }
    server.kill('SIGTERM');
    client.kill('SIGTERM');
    await Promise.allSettled([once(server, 'exit'), once(client, 'exit')]);
  }
});
