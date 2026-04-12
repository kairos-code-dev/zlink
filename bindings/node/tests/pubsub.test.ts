'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const net = require('node:net');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
const path = require('node:path');
const zlink = require('../dist/canonical');

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

  spot.setSubscription('topic');
  spot.unsetSubscription('topic');
  sub.setSubscription('topic');
  sub.unsetSubscription('topic');

  sub.close();
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
      serverSpot.publish(topic, 'payload');
      let received = null;
      try {
        received = clientSpot.subscribe(zlink.RecvFlags.DontWait);
      } catch (error) {
        if (!(error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData)) {
          throw error;
        }
      }
      if (received) {
        assert.equal(received.topic, topic);
        assert.deepEqual(
          received.parts.map((part) => part.data().toString()),
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

  const server = spawn(process.execPath, [serverPath, '--endpoint', endpoint], {
    cwd: path.join(__dirname, '..'),
    stdio: ['pipe', 'pipe', 'pipe'],
    detached: true
  });
  const client = spawn(process.execPath, [clientPath, '--endpoint', endpoint], {
    cwd: path.join(__dirname, '..'),
    stdio: ['ignore', 'pipe', 'pipe'],
    detached: true
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
    try {
      if (server.stdin.writable) {
        server.stdin.end();
      }
    } catch (_) {
      // ignore
    }
    try {
      process.kill(-server.pid, 'SIGKILL');
    } catch (_) {
      // ignore
    }
    try {
      process.kill(-client.pid, 'SIGKILL');
    } catch (_) {
      // ignore
    }
  }
});

test('canonical pub/sub surface hides opposite-direction methods', () => {
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);

  assert.equal(pub.recv, undefined);
  assert.equal(pub.send, undefined);
  assert.equal(sub.send, undefined);
  assert.equal(typeof sub.subscribe, 'function');

  sub.close();
  pub.close();
  ctx.close();
});

function subscribeMaybe(socket) {
  try {
    return socket.subscribe(zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

test('sub sockets receive Subscribed domain objects and non-blocking receive returns null when empty', () => {
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);

  pub.bind('inproc://subscribed-contract');
  sub.connect('inproc://subscribed-contract');
  sub.setSubscription('topic');

  assert.equal(subscribeMaybe(sub), null);

  pub.publish('topic', 'payload');

  const received = sub.subscribe();
  assert.equal(received.topic, 'topic');
  assert.equal(received.routingId, null);
  assert.deepEqual(received.parts.map((part) => part.data().toString()), ['payload']);

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

  const receivedPromise = new Promise((resolve, reject) => {
    try {
      sub.onSubscribe((routingId, topic, parts) => {
        resolve({ routingId, topic, parts });
      });
    } catch (err) {
      reject(err);
      return;
    }
  });

  await new Promise((resolve) => setTimeout(resolve, 50));
  pub.publish('topic', 'payload');
  const received = await receivedPromise;

  assert.ok(Buffer.isBuffer(received.routingId));
  assert.equal(received.topic, 'topic');
  assert.deepEqual(
    received.parts.map((part) => part.data().toString()),
    ['payload']
  );

  sub.close();
  pub.close();
  ctx.close();
});

test('onSubscribe blocks direct subscribe on the same socket', () => {
  const ctx = new zlink.Context();
  const sub = new zlink.SubSocket(ctx);

  sub.onSubscribe(() => {});

  assert.throws(() => sub.subscribe(), /busy|callback|state|subscribe/i);

  sub.close();
  ctx.close();
});
