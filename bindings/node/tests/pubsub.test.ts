'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const net = require('node:net');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
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

test('spot exposes unified publish and subscribe surface', () => {
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);
  const sub = new zlink.SubSocket(ctx);
  const monitor = spot.monitorOpen();

  spot.setSubscription('topic');
  spot.unsetSubscription('topic');
  sub.setSubscription('topic');
  sub.unsetSubscription('topic');

  assert.equal(typeof monitor.recv, 'function');

  sub.close();
  monitor.close();
  spot.close();
  node.close();
  ctx.close();
});

test('spot trySubscribe receives published payload after one immediate turn', async () => {
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);
  const topic = 'spot:direct';
  const monitor = spot.monitorOpen(zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED);

  spot.setSubscription(topic);
  while (true) {
    const event = monitor.recv();
    if (event.eventType === zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED) {
      break;
    }
  }
  spot.publish(topic, zlink.Message.copyOf('payload'));
  await new Promise((resolve) => setImmediate(resolve));

  const received = spot.trySubscribe();
  assert.notEqual(received, null);
  assert.equal(received.topic, topic);
  assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['payload']);

  monitor.close();
  spot.close();
  node.close();
  ctx.close();
});

test('spot onSubscribe delivers callback payloads', async () => {
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);
  const topic = 'spot:callback';
  const monitor = spot.monitorOpen(zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED);

  const received = await new Promise((resolve, reject) => {
    try {
      spot.onSubscribe((routingId, receivedTopic, parts) => {
        resolve({ routingId, receivedTopic, parts });
      });
    } catch (error) {
      reject(error);
      return;
    }

    spot.setSubscription(topic);
    while (true) {
      const event = monitor.recv();
      if (event.eventType === zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED) {
        break;
      }
    }
    spot.publish(topic, zlink.Message.copyOf('payload'));
  });

  assert.ok(received.routingId === null || Buffer.isBuffer(received.routingId));
  assert.equal(received.receivedTopic, topic);
  assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['payload']);

  monitor.close();
  spot.close();
  node.close();
  ctx.close();
});

test('remote spot peer delivery works over tcp direct peer connect', async () => {
  const ctx = new zlink.Context();
  const serverNode = new zlink.SpotNode(ctx);
  const clientNode = new zlink.SpotNode(ctx);
  const serverSpot = new zlink.Spot(serverNode);
  const clientSpot = new zlink.Spot(clientNode);
  const topic = 'spot:remote';
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;

  try {
    serverNode.bind(endpoint);
    clientNode.connectPeer(endpoint);
    clientSpot.setSubscription(topic);

    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      serverSpot.publish(topic, zlink.Message.copyOf('payload'));
      const received = clientSpot.trySubscribe();
      if (received) {
        assert.equal(received.topic, topic);
        assert.deepEqual(
          received.parts.map((part) => part.toBuffer().toString()),
          ['payload']
        );
        return;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }

    assert.fail(`remote spot delivery timeout: ${JSON.stringify({
      serverStatus: serverNode.statusSnapshot(),
      clientStatus: clientNode.statusSnapshot(),
      serverPeers: serverNode.peersSnapshot(),
      clientPeers: clientNode.peersSnapshot(),
      serverSubjects: serverNode.subjectsSnapshot(),
      clientSubjects: clientNode.subjectsSnapshot()
    })}`);
  } finally {
    clientSpot.close();
    serverSpot.close();
    clientNode.close();
    serverNode.close();
    ctx.close();
  }
});

test('spot node peersQuery filters manual peer connections', async () => {
  const ctx = new zlink.Context();
  const serverNode = new zlink.SpotNode(ctx);
  const clientNode = new zlink.SpotNode(ctx);
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;

  try {
    serverNode.bind(endpoint);
    clientNode.connectPeer(endpoint);

    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      const peers = clientNode.peersQuery({ peerEndpoint: endpoint });
      if (peers.length > 0) {
        assert.equal(peers[0].peerEndpoint, endpoint);
        assert.equal(peers[0].source, zlink.SpotPeerSource.MANUAL);
        return;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }

    assert.fail(`spot peersQuery timeout: ${JSON.stringify({
      serverStatus: serverNode.statusSnapshot(),
      clientStatus: clientNode.statusSnapshot(),
      clientPeers: clientNode.peersSnapshot()
    })}`);
  } finally {
    clientNode.close();
    serverNode.close();
    ctx.close();
  }
});

test('multiple remote spot peers on one context all receive over tcp direct peer connect', async () => {
  const ctx = new zlink.Context();
  const serverNode = new zlink.SpotNode(ctx);
  const serverSpot = new zlink.Spot(serverNode);
  const topic = 'spot:remote:multi';
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const clientNodes = [];
  const clientSpots = [];

  try {
    serverNode.bind(endpoint);
    for (let i = 0; i < 2; i += 1) {
      const clientNode = new zlink.SpotNode(ctx);
      const clientSpot = new zlink.Spot(clientNode);
      clientNode.connectPeer(endpoint);
      clientSpot.setSubscription(topic);
      clientNodes.push(clientNode);
      clientSpots.push(clientSpot);
    }

    const received = new Set();
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      serverSpot.publish(topic, zlink.Message.copyOf('payload'));
      for (let i = 0; i < clientSpots.length; i += 1) {
        if (received.has(i)) {
          continue;
        }
        const subscribed = clientSpots[i].trySubscribe();
        if (!subscribed) {
          continue;
        }
        assert.equal(subscribed.topic, topic);
        assert.deepEqual(
          subscribed.parts.map((part) => part.toBuffer().toString()),
          ['payload']
        );
        received.add(i);
      }
      if (received.size === clientSpots.length) {
        return;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }

    assert.fail(`multi remote spot delivery timeout: ${JSON.stringify({
      serverStatus: serverNode.statusSnapshot(),
      serverPeers: serverNode.peersSnapshot(),
      serverSubjects: serverNode.subjectsSnapshot(),
      clientStates: clientNodes.map((clientNode) => ({
        status: clientNode.statusSnapshot(),
        peers: clientNode.peersSnapshot(),
        subjects: clientNode.subjectsSnapshot()
      }))
    })}`);
  } finally {
    for (const clientSpot of clientSpots) {
      clientSpot.close();
    }
    for (const clientNode of clientNodes) {
      clientNode.close();
    }
    serverSpot.close();
    serverNode.close();
    ctx.close();
  }
});

test('remote spot peer delivery works across child processes', async () => {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const fixturesDir = path.join(__dirname, '..', '..', 'tests', 'fixtures');
  const serverPath = path.join(fixturesDir, 'spot_child_server.js');
  const clientPath = path.join(fixturesDir, 'spot_child_client.js');

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

  const waitForExit = (child) => {
    if (child.exitCode !== null || child.signalCode !== null) {
      return Promise.resolve();
    }
    return once(child, 'exit').then(() => {});
  };

  const server = spawn(process.execPath, [serverPath, '--endpoint', endpoint], {
    cwd: path.join(__dirname, '..'),
    stdio: ['pipe', 'pipe', 'pipe']
  });
  const client = spawn(process.execPath, [clientPath, '--endpoint', endpoint], {
    cwd: path.join(__dirname, '..'),
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
    server.stdin.write('START\n');
    await waitForLine(client, 'RECEIVED,spot:child,payload', 5000, () => clientStderr);

    const [clientCode] = await once(client, 'exit');
    assert.equal(clientCode, 0, clientStderr);
  } finally {
    if (server.stdin.writable) {
      server.stdin.end();
    }
    server.kill('SIGTERM');
    client.kill('SIGTERM');
    await Promise.allSettled([waitForExit(server), waitForExit(client)]);
  }
});

test('canonical pub/sub surface hides opposite-direction methods', () => {
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);

  assert.equal(pub.recv, undefined);
  assert.equal(typeof pub.tryPublish, 'function');
  assert.equal(pub.send, undefined);
  assert.equal(sub.send, undefined);
  assert.equal(typeof sub.subscribe, 'function');

  sub.close();
  pub.close();
  ctx.close();
});

test('sub sockets receive Subscribed domain objects and TrySubscribe returns null when empty', () => {
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);

  pub.bind('inproc://subscribed-contract');
  sub.connect('inproc://subscribed-contract');
  sub.setSubscription('topic');

  assert.equal(sub.trySubscribe(), null);

  pub.publish('topic', zlink.Message.copyOf('payload'));

  const received = sub.subscribe();
  assert.equal(received.topic, 'topic');
  assert.equal(received.routingId, null);
  assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['payload']);

  sub.close();
  pub.close();
  ctx.close();
});

test('onSubscribe delivers topic-aware multipart payloads', async () => {
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);

  pub.bind('inproc://subscribe-handler-contract');
  sub.connect('inproc://subscribe-handler-contract');
  sub.setSubscription('topic');

  const received = await new Promise((resolve, reject) => {
    try {
      sub.onSubscribe((routingId, topic, parts) => {
        resolve({ routingId, topic, parts });
      });
    } catch (err) {
      reject(err);
      return;
    }
    pub.publish('topic', zlink.Message.copyOf('payload'));
  });

  assert.equal(received.routingId, null);
  assert.equal(received.topic, 'topic');
  assert.deepEqual(
    received.parts.map((part) => part.toBuffer().toString()),
    ['payload']
  );

  sub.close();
  pub.close();
  ctx.close();
});
