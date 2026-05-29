'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const net = require('node:net');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
const path = require('node:path');
const zlink = require('@zlink-systems/zlink');

const SPOT_PEER_SOURCE_MANUAL = 1;
const SPOT_CHANNEL_NAME = 'pubsub-spot-service';

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise<void>((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function setPubBindOnReservedPort(node) {
  let lastError;
  for (let attempt = 0; attempt < 8; attempt += 1) {
    const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
    try {
      node.setPubBind(endpoint);
      return endpoint;
    } catch (error) {
      lastError = error;
      if (!/Address already in use|EADDRINUSE/i.test(String(error?.message ?? error))) {
        throw error;
      }
    }
  }
  throw lastError;
}

test('spot exposes unified publish and subscribe surface', () => {
  const ctx = zlink.createContext();
  const node = zlink.createSpotNode(ctx);
  const spot = node.createSpot();
  const sub = zlink.createSubSocket(ctx);

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
  const ctx = zlink.createContext();
  const serverNode = zlink.createSpotNode(ctx);
  const clientNode = zlink.createSpotNode(ctx);
  const topic = 'spot:remote';
  let serverEndpoint = '';
  let clientEndpoint = '';
  let serverSpot;
  let clientSpot;

  try {
    serverEndpoint = await setPubBindOnReservedPort(serverNode);
    clientEndpoint = await setPubBindOnReservedPort(clientNode);
    serverNode.connectPeer(clientEndpoint);
    clientNode.connectPeer(serverEndpoint);
    serverSpot = serverNode.createSpot();
    clientSpot = clientNode.createSpot();
    clientSpot.setSubscription(topic);

    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      if (serverNode.status().connectedPeerCount > 0
          && clientNode.status().connectedPeerCount > 0) {
        serverSpot.publish(topic).message(Buffer.from('payload')).submit();
      }
      const received = new zlink.TopicMessage();
      try {
        if (!clientSpot.subscribe(received, zlink.RecvFlags.DontWait)) {
          await new Promise((resolve) => setTimeout(resolve, 25));
          continue;
        }
      } catch (error) {
        if (!(error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData)) {
          throw error;
        }
      }
      assert.equal(received.topic, topic);
      assert.deepEqual(
        received.parts.map((part) => part.data().toString()),
        ['payload']
      );
      serverSpot.publish(topic).message(Buffer.from('payload-into')).submit();
      const payloadDeadline = Date.now() + 5000;
      while (Date.now() < payloadDeadline) {
        serverSpot.publish(topic).message(Buffer.from('payload-into')).submit();
        const payload = new zlink.TopicMessage();
        if (!clientSpot.subscribe(payload, zlink.RecvFlags.DontWait)) {
          await new Promise((resolve) => setTimeout(resolve, 25));
          continue;
        }
        if (payload.singlePartOrThrow().size() !== 12) {
          await new Promise((resolve) => setTimeout(resolve, 25));
          continue;
        }
        assert.equal(payload.topic, topic);
        assert.equal(payload.singlePartOrThrow().data().toString(), 'payload-into');
        return;
      }
      assert.fail('spot subscribe timeout');
      await new Promise((resolve) => setImmediate(resolve));
    }

    assert.fail(`remote spot delivery timeout: ${JSON.stringify({
      serverStatus: serverNode.status(),
      clientStatus: clientNode.status(),
      serverPeers: serverNode.peers(),
      clientPeers: clientNode.peers(),
      serverSubjects: serverNode.subjects(),
      clientSubjects: clientNode.subjects()
    })}`);
  } finally {
    if (clientSpot) {
      clientSpot.close();
    }
    if (serverSpot) {
      serverSpot.close();
    }
    serverNode.close();
    clientNode.close();
    ctx.close();
  }
});

test('spot node peersQuery filters manual peer connections', async () => {
  const ctx = zlink.createContext();
  const serverNode = zlink.createSpotNode(ctx);
  const clientNode = zlink.createSpotNode(ctx);
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;

  try {
    serverNode.setPubBind(endpoint);
    clientNode.connectPeer(endpoint);

    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      const peers = clientNode.peersQuery({ peerEndpoint: endpoint });
      if (peers.length > 0) {
        assert.equal(peers[0].peerEndpoint, endpoint);
        assert.equal(peers[0].source, SPOT_PEER_SOURCE_MANUAL);
        return;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }

    assert.fail(`spot peersQuery timeout: ${JSON.stringify({
      serverStatus: serverNode.status(),
      clientStatus: clientNode.status(),
      clientPeers: clientNode.peers()
    })}`);
  } finally {
    clientNode.close();
    serverNode.close();
    ctx.close();
  }
});

test('remote spot peer delivery works across child processes', async () => {
  const serverEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const clientEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const fixturesDir = path.join(__dirname, '..', '..', 'tests', 'fixtures');
  const serverPath = path.join(fixturesDir, 'spot_child_server.js');
  const clientPath = path.join(fixturesDir, 'spot_child_client.js');

  const waitForLine = (child, expected, timeoutMs, sink) => {
    return new Promise<void>((resolve, reject) => {
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

  const server = spawn(process.execPath, [
    serverPath,
    '--bind-endpoint', serverEndpoint,
    '--peer-endpoint', clientEndpoint
  ], {
    cwd: path.join(__dirname, '..'),
    stdio: ['pipe', 'pipe', 'pipe'],
    detached: true
  });
  const client = spawn(process.execPath, [
    clientPath,
    '--bind-endpoint', clientEndpoint,
    '--peer-endpoint', serverEndpoint
  ], {
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
    await waitForLine(server, `READY,${serverEndpoint}`, 5000, () => serverStderr);
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
  const ctx = zlink.createContext();
  const pub = zlink.createPubSocket(ctx);
  const sub = zlink.createSubSocket(ctx);

  assert.equal(pub.recv, undefined);
  assert.equal(pub.send, undefined);
  assert.equal(sub.send, undefined);
  assert.equal(typeof sub.subscribe, 'function');
  assert.equal(sub.subscribePayloadInto, undefined);

  sub.close();
  pub.close();
  ctx.close();
});

function subscribeMaybe(socket) {
  try {
    const received = new zlink.TopicMessage();
    return socket.subscribe(received, zlink.RecvFlags.DontWait) ? received : null;
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

test('sub sockets receive TopicMessage domain objects and non-blocking receive returns null when empty', () => {
  const ctx = zlink.createContext();
  const pub = zlink.createPubSocket(ctx);
  const sub = zlink.createSubSocket(ctx);

  pub.bind('inproc://subscribed-contract');
  sub.connect('inproc://subscribed-contract');
  sub.setSubscription('topic');

  assert.equal(subscribeMaybe(sub), null);

  pub.publish('topic').message('payload').submit();

  const received = new zlink.TopicMessage();
  assert.equal(sub.subscribe(received), true);
  assert.equal(received.topic, 'topic');
  assert.ok(received.routingId === null || received.routingId instanceof zlink.RoutingId);
  assert.deepEqual(received.parts.map((part) => part.data().toString()), ['payload']);

  pub.publish('topic').message('payload-into').submit();
  const payload = new zlink.TopicMessage();
  assert.equal(sub.subscribe(payload), true);
  assert.equal(payload.topic, 'topic');
  assert.equal(payload.routingId, null);
  assert.equal(payload.singlePartOrThrow().data().toString(), 'payload-into');

  sub.close();
  pub.close();
  ctx.close();
});

test('subscribe returns topic-aware multipart payloads without callback mode', async () => {
  const ctx = zlink.createContext();
  const pub = zlink.createPubSocket(ctx);
  const sub = zlink.createSubSocket(ctx);

  pub.bind('inproc://subscribe-handler-contract');
  sub.connect('inproc://subscribe-handler-contract');
  sub.setSubscription('topic');
  pub.publish('topic').message('payload').submit();
  const deadline = Date.now() + 5000;
  let received = null;
  while (Date.now() < deadline) {
    try {
      const candidate = new zlink.TopicMessage();
      if (sub.subscribe(candidate, zlink.RecvFlags.DontWait)) {
        received = candidate;
        break;
      }
    } catch (error) {
      if (!(error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData)) {
        throw error;
      }
    }
    await new Promise((resolve) => setTimeout(resolve, 25));
  }

  assert.notEqual(received, null);
  assert.ok(received.routingId === null || received.routingId instanceof zlink.RoutingId);
  assert.equal(received.topic, 'topic');
  assert.deepEqual(
    received.parts.map((part) => part.data().toString()),
    ['payload']
  );

  pub.publish('topic').message('payload-into').submit();
  let payload = null;
  while (Date.now() < deadline) {
    try {
      const candidate = new zlink.TopicMessage();
      if (sub.subscribe(candidate, zlink.RecvFlags.DontWait)) {
        payload = candidate;
        break;
      }
    } catch (error) {
      if (!(error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData)) {
        throw error;
      }
    }
    await new Promise((resolve) => setTimeout(resolve, 25));
  }
  assert.notEqual(payload, null);
  assert.equal(payload.topic, 'topic');
  assert.equal(payload.routingId, null);
  assert.equal(payload.singlePartOrThrow().data().toString(), 'payload-into');

  sub.close();
  pub.close();
  ctx.close();
});

test('sub sockets do not expose callback subscription surfaces', () => {
  const ctx = zlink.createContext();
  const sub = zlink.createSubSocket(ctx);

  assert.equal(sub.onSubscribe, undefined);
  assert.equal(typeof sub.subscribe, 'function');
  assert.equal(sub.subscribePayloadInto, undefined);

  sub.close();
  ctx.close();
});
