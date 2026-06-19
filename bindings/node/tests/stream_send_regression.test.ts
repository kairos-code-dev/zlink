// SPDX-License-Identifier: MPL-2.0

'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('@zlink-systems/zlink');

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise<void>((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
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

function frame(header, payload) {
  const headerBytes = Buffer.from(header);
  const payloadBytes = Buffer.from(payload);
  const output = Buffer.alloc(6 + headerBytes.length + payloadBytes.length);
  output.writeUInt16BE(headerBytes.length, 0);
  output.writeUInt32BE(payloadBytes.length, 2);
  headerBytes.copy(output, 6);
  payloadBytes.copy(output, 6 + headerBytes.length);
  return output;
}

async function readFrames(socket, count, timeoutMs = 5000) {
  let buffered = Buffer.alloc(0);
  const frames = [];
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline && frames.length < count) {
    const remaining = deadline - Date.now();
    const chunk = await Promise.race([
      once(socket, 'data').then(([data]) => data),
      new Promise((_, reject) => setTimeout(() => reject(new Error('read timed out')), remaining))
    ]);
    buffered = Buffer.concat([buffered, chunk]);
    for (;;) {
      if (buffered.length < 6) {
        break;
      }
      const headerLength = buffered.readUInt16BE(0);
      const payloadLength = buffered.readUInt32BE(2);
      const frameLength = 6 + headerLength + payloadLength;
      if (buffered.length < frameLength) {
        break;
      }
      const current = buffered.subarray(0, frameLength);
      frames.push({
        header: current.subarray(6, 6 + headerLength).toString(),
        payload: current.subarray(6 + headerLength).toString()
      });
      buffered = buffered.subarray(frameLength);
      if (frames.length >= count) {
        break;
      }
    }
  }
  return frames;
}

test('stream routed send delivers consecutive frames to the same raw client', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const ctx = zlink.createContext();
  const stream = zlink.createStreamSocket(ctx);
  const client = new net.Socket();

  try {
    stream.bind(endpoint);
    let sourceRid;
    const received = new Promise<void>((resolve) => {
      stream.setPacketHandler((rid, header, body) => {
        sourceRid = rid;
        assert.equal(header.data().toString(), 'request-header');
        assert.equal(body.data().toString(), 'request-body');
        header.close();
        body.close();
        resolve();
      });
    });

    await new Promise<void>((resolve, reject) => {
      client.once('error', reject);
      client.connect(Number(endpoint.split(':').pop()), '127.0.0.1', resolve);
    });
    client.write(frame('request-header', 'request-body'));
    await received;

    assert.ok(sourceRid);
    stream.send(sourceRid).message(frame('response-1', 'payload-1')).submit();
    stream.send(sourceRid).message(frame('notify-2', 'payload-2')).submit();
    stream.send(sourceRid).message(frame('notify-3', 'payload-3')).submit();

    assert.deepEqual(await readFrames(client, 3), [
      { header: 'response-1', payload: 'payload-1' },
      { header: 'notify-2', payload: 'payload-2' },
      { header: 'notify-3', payload: 'payload-3' }
    ]);
  } finally {
    client.destroy();
    stream.close();
    ctx.close();
  }
});

test('stream routed send from spot dispatch delivers repeated frames to raw client', async () => {
  const streamEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const routerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const ctx = zlink.createContext();
  const stream = zlink.createStreamSocket(ctx);
  const node = zlink.createSpotNode(ctx);
  const spot = node.createSpot();
  const router = zlink.createRouterSocket(ctx);
  const client = new net.Socket();

  try {
    stream.bind(streamEndpoint);
    router.bind(routerEndpoint);
    node.connectRouterChannelPeerRid('api', node.routingId, routerEndpoint);

    let streamRid;
    const streamReady = new Promise<void>((resolve) => {
      stream.setPacketHandler((rid, header, body) => {
        streamRid = rid;
        header.close();
        body.close();
        resolve();
      });
    });
    await new Promise<void>((resolve, reject) => {
      client.once('error', reject);
      client.connect(Number(streamEndpoint.split(':').pop()), '127.0.0.1', resolve);
    });
    client.write(frame('stream-header', 'stream-body'));
    await streamReady;
    assert.ok(streamRid);

    let handled = 0;
    spot.setDispatchHandler((info) => {
      if (info.event !== zlink.SpotDispatchEvent.RoutedReadable) {
        return;
      }
      try {
        handled += 1;
        stream.send(streamRid)
          .message(frame(`notify-${handled}`, `payload-${handled}`))
          .submit();
        info.routed.reply()
          .message(Buffer.from(`reply-${handled}`))
          .submit();
      } finally {
        info.routed?.close();
      }
    });

    await waitFor(
      () => node.peers().some((peer) => peer.channelName === 'api'
        && peer.kind === zlink.SpotPeerKind.RouterChannel),
      'spot router channel peer connection'
    );

    const frameReads = readFrames(client, 3);
    for (let i = 1; i <= 3; i += 1) {
      const reply = await router.requestToSpot(node.routingId, spot.routingId)
        .message(Buffer.from(`request-${i}`))
        .timeout(2000)
        .submit();
      assert.equal(reply.length, 1);
      assert.equal(reply[0].data().toString(), `reply-${i}`);
      reply[0].close();
    }

    assert.deepEqual(await frameReads, [
      { header: 'notify-1', payload: 'payload-1' },
      { header: 'notify-2', payload: 'payload-2' },
      { header: 'notify-3', payload: 'payload-3' }
    ]);
  } finally {
    client.destroy();
    router.close();
    spot.close();
    node.close();
    stream.close();
    ctx.close();
  }
});

test('stream routed send from spot-to-spot dispatch delivers repeated frames to raw client', async () => {
  const streamEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const responderPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const responderRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const requesterPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const requesterRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const ctx = zlink.createContext();
  const stream = zlink.createStreamSocket(ctx);
  const responderNode = zlink.createSpotNode(ctx);
  const requesterNode = zlink.createSpotNode(ctx);
  const responder = responderNode.entrySpot();
  const requester = requesterNode.entrySpot();
  const client = new net.Socket();

  try {
    responderNode.setRoutingId(zlink.RoutingId.from(Buffer.from('stream-responder-node')));
    requesterNode.setRoutingId(zlink.RoutingId.from(Buffer.from('stream-requester-node')));
    stream.bind(streamEndpoint);
    responderNode.setRouterBind(responderRouterEndpoint);
    responderNode.setPubBind(responderPeerEndpoint);
    requesterNode.setRouterBind(requesterRouterEndpoint);
    requesterNode.setPubBind(requesterPeerEndpoint);
    responderNode.connectPeer(requesterPeerEndpoint);
    requesterNode.connectPeer(responderPeerEndpoint);

    let streamRid;
    const streamReady = new Promise<void>((resolve) => {
      stream.setPacketHandler((rid, header, body) => {
        streamRid = rid;
        header.close();
        body.close();
        resolve();
      });
    });
    await new Promise<void>((resolve, reject) => {
      client.once('error', reject);
      client.connect(Number(streamEndpoint.split(':').pop()), '127.0.0.1', resolve);
    });
    client.write(frame('stream-header', 'stream-body'));
    await streamReady;

    let handled = 0;
    responder.setDispatchHandler((info) => {
      if (info.event !== zlink.SpotDispatchEvent.RoutedReadable) {
        return;
      }
      try {
        handled += 1;
        stream.send(streamRid)
          .message(frame(`spot-notify-${handled}`, `payload-${handled}`))
          .submit();
        info.routed.reply()
          .message(Buffer.from(`spot-reply-${handled}`))
          .submit();
      } finally {
        info.routed?.close();
      }
    });

    await Promise.all([
      waitFor(() => responderNode.status().connectedPeerCount > 0, 'responder peer connection'),
      waitFor(() => requesterNode.status().connectedPeerCount > 0, 'requester peer connection')
    ]);

    const frameReads = readFrames(client, 3);
    for (let i = 1; i <= 3; i += 1) {
      const reply = await requester.requestToSpot(responderNode.routingId, responder.routingId)
        .message(Buffer.from(`spot-request-${i}`))
        .timeout(2000)
        .submit();
      assert.equal(reply.length, 1);
      assert.equal(reply[0].data().toString(), `spot-reply-${i}`);
      reply[0].close();
    }

    assert.deepEqual(await frameReads, [
      { header: 'spot-notify-1', payload: 'payload-1' },
      { header: 'spot-notify-2', payload: 'payload-2' },
      { header: 'spot-notify-3', payload: 'payload-3' }
    ]);
  } finally {
    client.destroy();
    requester.close();
    responder.close();
    requesterNode.close();
    responderNode.close();
    stream.close();
    ctx.close();
  }
});

test('actor bound session send from spot-to-spot dispatch flushes final frame without later peer activity', async () => {
  const streamEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const responderPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const responderRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const requesterPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const requesterRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const ctx = zlink.createContext();
  const stream = zlink.createStreamSocket(ctx);
  const responderNode = zlink.createSpotNode(ctx);
  const requesterNode = zlink.createSpotNode(ctx);
  const responder = responderNode.entrySpot();
  const requester = requesterNode.entrySpot();
  const actor = responderNode.createActor('bound-target');
  const client = new net.Socket();

  try {
    responderNode.setRoutingId(zlink.RoutingId.from(Buffer.from('bound-responder-node')));
    requesterNode.setRoutingId(zlink.RoutingId.from(Buffer.from('bound-requester-node')));
    stream.bind(streamEndpoint);
    stream.attachActorGateway(responderNode);
    responderNode.setRouterBind(responderRouterEndpoint);
    responderNode.setPubBind(responderPeerEndpoint);
    requesterNode.setRouterBind(requesterRouterEndpoint);
    requesterNode.setPubBind(requesterPeerEndpoint);
    responderNode.connectPeer(requesterPeerEndpoint);
    requesterNode.connectPeer(responderPeerEndpoint);

    let streamRid;
    const streamReady = new Promise<void>((resolve) => {
      stream.setPacketHandler((rid, header, body) => {
        streamRid = rid;
        header.close();
        body.close();
        resolve();
      });
    });
    await new Promise<void>((resolve, reject) => {
      client.once('error', reject);
      client.connect(Number(streamEndpoint.split(':').pop()), '127.0.0.1', resolve);
    });
    client.write(frame('stream-header', 'stream-body'));
    await streamReady;
    await stream.bindActor(streamRid, actor.ref()).timeout(2000).submit();

    let handled = 0;
    responder.setDispatchHandler((info) => {
      if (info.event !== zlink.SpotDispatchEvent.RoutedReadable) {
        return;
      }
      try {
        handled += 1;
        responderNode.sendActorBoundSession(actor.ref())
          .message(frame(`bound-notify-${handled}`, `payload-${handled}`))
          .submit();
        info.routed.reply()
          .message(Buffer.from(`bound-reply-${handled}`))
          .submit();
      } finally {
        info.routed?.close();
      }
    });

    await Promise.all([
      waitFor(() => responderNode.status().connectedPeerCount > 0, 'responder peer connection'),
      waitFor(() => requesterNode.status().connectedPeerCount > 0, 'requester peer connection')
    ]);

    const frameReads = readFrames(client, 3);
    for (let i = 1; i <= 3; i += 1) {
      const reply = await requester.requestToSpot(responderNode.routingId, responder.routingId)
        .message(Buffer.from(`bound-request-${i}`))
        .timeout(2000)
        .submit();
      assert.equal(reply.length, 1);
      assert.equal(reply[0].data().toString(), `bound-reply-${i}`);
      reply[0].close();
    }

    assert.deepEqual(await frameReads, [
      { header: 'bound-notify-1', payload: 'payload-1' },
      { header: 'bound-notify-2', payload: 'payload-2' },
      { header: 'bound-notify-3', payload: 'payload-3' }
    ]);
  } finally {
    client.destroy();
    actor.close(0);
    requester.close();
    responder.close();
    requesterNode.close();
    responderNode.close();
    stream.close();
    ctx.close();
  }
});
