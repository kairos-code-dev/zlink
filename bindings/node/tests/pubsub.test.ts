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

async function waitFor(condition, label, timeoutMs = 5000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (condition()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  throw new Error(`${label} timed out`);
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
        serverSpot.publish(topic)
          .message(zlink.Message.from(Buffer.from('header')))
          .message(zlink.Message.from(Buffer.from('payload')))
          .submit();
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
        ['header', 'payload']
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
        if (payload.parts.length !== 1 || payload.parts[0].data().toString() !== 'payload-into') {
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

test('spot publish is delivered to peers that connected to the publisher bind', async () => {
  const ctx = zlink.createContext();
  const nodeA = zlink.createSpotNode(ctx);
  const nodeB = zlink.createSpotNode(ctx);
  const topic = 'spot:direction';
  let endpointA = '';
  let endpointB = '';
  let spotA;
  let spotB;

  try {
    endpointA = await setPubBindOnReservedPort(nodeA);
    endpointB = await setPubBindOnReservedPort(nodeB);
    nodeA.connectPeer(endpointB);
    nodeB.connectPeer(endpointA);
    spotA = nodeA.createSpot();
    spotB = nodeB.entrySpot();
    spotB.setSubscription(topic);

    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      if (nodeA.status().connectedPeerCount > 0 && nodeB.status().connectedPeerCount > 0) {
        spotA.publish(topic).message(zlink.Message.from(Buffer.from('from-a'))).submit();
      }
      const received = new zlink.TopicMessage();
      if (spotB.subscribe(received, zlink.RecvFlags.DontWait)) {
        try {
          assert.equal(received.topic, topic);
          assert.deepEqual(received.parts.map((part) => part.data().toString()), ['from-a']);
          return;
        } finally {
          received.close();
        }
      }
      received.close();
      await new Promise((resolve) => setTimeout(resolve, 25));
    }

    assert.fail('spot publish direction timeout');
  } finally {
    spotB?.close();
    spotA?.close();
    nodeB.close();
    nodeA.close();
    ctx.close();
  }
});

test('spot publish is delivered to local entry spot multipart subscriber', async () => {
  const ctx = zlink.createContext();
  const node = zlink.createSpotNode(ctx);
  const publisher = node.createSpot();
  const subscriber = node.entrySpot();
  const topic = 'spot:local-entry';

  try {
    subscriber.setSubscription(topic);
    publisher.publish(topic)
      .message(zlink.Message.from(Buffer.from('header')))
      .message(zlink.Message.from(Buffer.from('payload')))
      .submit();

    const received = new zlink.TopicMessage();
    const deadline = Date.now() + 2000;
    while (Date.now() < deadline) {
      if (subscriber.subscribe(received, zlink.RecvFlags.DontWait)) {
        assert.equal(received.topic, topic);
        assert.deepEqual(
          received.parts.map((part) => part.data().toString()),
          ['header', 'payload']
        );
        return;
      }
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
    assert.fail('local entry spot subscribe timeout');
  } finally {
    subscriber.close();
    publisher.close();
    node.close();
    ctx.close();
  }
});

test('spot publish reaches local and peer entry spot subscribers for same topic', async () => {
  const ctx = zlink.createContext();
  const nodeA = zlink.createSpotNode(ctx);
  const nodeB = zlink.createSpotNode(ctx);
  const topic = 'spot:local-and-peer';
  let endpointA = '';
  let endpointB = '';
  let publisher;
  let localSubscriber;
  let peerSubscriber;

  try {
    endpointA = await setPubBindOnReservedPort(nodeA);
    endpointB = await setPubBindOnReservedPort(nodeB);
    nodeA.connectPeer(endpointB);
    nodeB.connectPeer(endpointA);
    publisher = nodeA.createSpot();
    localSubscriber = nodeA.entrySpot();
    peerSubscriber = nodeB.entrySpot();
    localSubscriber.setSubscription(topic);
    peerSubscriber.setSubscription(topic);

    await waitFor(
      () => nodeA.status().connectedPeerCount > 0 && nodeB.status().connectedPeerCount > 0,
      'local and peer spot connection'
    );

    publisher.publish(topic)
      .message(zlink.Message.from(Buffer.from('header')))
      .message(zlink.Message.from(Buffer.from('payload')))
      .submit();

    const local = new zlink.TopicMessage();
    const peer = new zlink.TopicMessage();
    const deadline = Date.now() + 5000;
    let sawLocal = false;
    let sawPeer = false;
    while (Date.now() < deadline && (!sawLocal || !sawPeer)) {
      if (!sawLocal && localSubscriber.subscribe(local, zlink.RecvFlags.DontWait)) {
        sawLocal = true;
      }
      if (!sawPeer && peerSubscriber.subscribe(peer, zlink.RecvFlags.DontWait)) {
        sawPeer = true;
      }
      if (!sawLocal || !sawPeer) {
        await new Promise((resolve) => setTimeout(resolve, 10));
      }
    }
    assert.equal(sawLocal, true);
    assert.equal(sawPeer, true);
    assert.deepEqual(local.parts.map((part) => part.data().toString()), ['header', 'payload']);
    assert.deepEqual(peer.parts.map((part) => part.data().toString()), ['header', 'payload']);
  } finally {
    peerSubscriber?.close();
    localSubscriber?.close();
    publisher?.close();
    nodeB.close();
    nodeA.close();
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
  const waitForServerLine = createChildLineWaiter(server);
  const waitForClientLine = createChildLineWaiter(client);

  try {
    await waitForServerLine(`READY,${serverEndpoint}`, 5000, () => serverStderr);
    await waitForClientLine('CLIENT_READY', 5000, () => clientStderr);
    server.stdin.write('START\n');
    await waitForClientLine('RECEIVED,spot:child,payload', 5000, () => clientStderr);

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

function createChildLineWaiter(child) {
  const queued = [];
  const waiters = [];
  let buffered = '';
  let exited = false;
  let exitCode = null;

  const tryResolve = (waiter) => {
    const index = queued.indexOf(waiter.expected);
    if (index < 0) {
      return false;
    }
    queued.splice(0, index + 1);
    clearTimeout(waiter.timeout);
    waiter.resolve();
    return true;
  };
  const flushWaiters = () => {
    for (let index = 0; index < waiters.length;) {
      if (tryResolve(waiters[index])) {
        waiters.splice(index, 1);
      } else {
        index += 1;
      }
    }
  };

  child.stdout.on('data', (chunk) => {
    buffered += chunk.toString();
    while (true) {
      const newline = buffered.indexOf('\n');
      if (newline === -1) {
        break;
      }
      const line = buffered.slice(0, newline).trim();
      buffered = buffered.slice(newline + 1);
      if (line) {
        queued.push(line);
      }
    }
    flushWaiters();
  });
  child.once('exit', (code) => {
    exited = true;
    exitCode = code;
    for (const waiter of waiters.splice(0)) {
      clearTimeout(waiter.timeout);
      waiter.reject(new Error(`exited before ${waiter.expected}: ${exitCode}: ${waiter.sink()}`));
    }
  });

  return (expected, timeoutMs, sink) => new Promise<void>((resolve, reject) => {
    const waiter = {
      expected,
      sink,
      resolve,
      reject,
      timeout: setTimeout(() => {
        const index = waiters.indexOf(waiter);
        if (index >= 0) {
          waiters.splice(index, 1);
        }
        reject(new Error(`timeout waiting for ${expected}: ${sink()}`));
      }, timeoutMs)
    };
    if (tryResolve(waiter)) {
      return;
    }
    if (exited) {
      clearTimeout(waiter.timeout);
      reject(new Error(`exited before ${expected}: ${exitCode}: ${sink()}`));
      return;
    }
    waiters.push(waiter);
  });
}

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
