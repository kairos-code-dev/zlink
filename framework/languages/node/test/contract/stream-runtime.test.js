const assert = require('node:assert/strict');
const test = require('node:test');

const connector = require('../../packages/stream-connector/dist');
const framework = require('../../packages/framework/dist/internal');

test('stream runtime is exported from framework root surface', () => {
  assert.equal(typeof framework.ZLinkStreamBindingRuntime, 'function');
  assert.equal(typeof framework.DefaultZLinkSessionContext, 'function');
});

test('ZLinkStreamBindingRuntime creates dotnet-shaped session context and closes through stream', async () => {
  let closed = 0;
  const runtime = new framework.ZLinkStreamBindingRuntime();
  const context = runtime.createSessionContext({
    sessionId: 'session-1',
    routingId: 'rid-1',
    localAddr: 'tcp://local',
    remoteAddr: 'tcp://remote',
    write() {
      return true;
    },
    async close() {
      closed += 1;
    }
  });

  assert.equal(context.sessionId, 'session-1');
  assert.equal(context.routingId, 'rid-1');
  assert.equal(context.localAddr, 'tcp://local');
  assert.equal(context.remoteAddr, 'tcp://remote');
  await context.close();
  assert.equal(closed, 1);
});

test('session actors bind actor refs, expose bound actors, and reject missing routing id', async () => {
  const runtime = new framework.ZLinkStreamBindingRuntime();
  const context = runtime.createSessionContext(fakeStream('session-2', 'rid-2'));

  const actor = await context.actors.bind({
    nodeRid: 'node-a',
    actorId: 'actor-a',
    generation: 1
  });

  assert.equal(actor.actorId, 'actor-a');
  assert.equal(context.actors.find('actor-a'), actor);
  assert.deepEqual(context.actors.bound.map((entry) => entry.actorId), ['actor-a']);

  const missingRoutingContext = runtime.createSessionContext(fakeStream('session-3', undefined));
  await assert.rejects(
    () => missingRoutingContext.actors.bind({ nodeRid: 'node-a', actorId: 'actor-b', generation: 1 }),
    /routing id/
  );
});

test('managed stream actor bind calls native ActorGateway before local binding is visible', async () => {
  const socket = new FakeStreamSocket();
  const runtime = new framework.ZLinkStreamBindingRuntime({ actorBindTimeoutMs: 1234 });
  const context = runtime.createSessionContext(new framework.ZLinkManagedStream(socket, 'backend-rid', 'public-session'));
  const actorRef = { nodeRid: 'node-a', actorId: 'actor-a', generation: 1n };

  const actor = await context.actors.bind(actorRef);

  assert.equal(actor.actorId, 'actor-a');
  assert.equal(socket.boundActors.length, 1);
  assert.deepEqual(socket.boundActors[0], {
    sessionRid: 'backend-rid',
    actor: actorRef,
    timeoutMs: 1234
  });
  assert.equal(context.actors.find('actor-a'), actor);
  assert.equal(runtime.find('actor-a'), actor);
});

test('managed stream actor bind failure does not create stale local binding', async () => {
  const socket = new FakeStreamSocket();
  socket.bindError = new Error('native bind failed');
  const runtime = new framework.ZLinkStreamBindingRuntime();
  const context = runtime.createSessionContext(new framework.ZLinkManagedStream(socket, 'backend-rid', 'public-session'));

  await assert.rejects(
    () => context.actors.bind({ nodeRid: 'node-a', actorId: 'actor-a', generation: 1n }),
    /native bind failed/
  );

  assert.equal(context.actors.find('actor-a'), undefined);
  assert.equal(runtime.find('actor-a'), undefined);
});

test('session actor relay sends header and payload through managed stream ActorGateway route', async () => {
  const socket = new FakeStreamSocket();
  const runtime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: binaryMessageFactory()
  });
  const context = runtime.createSessionContext(new framework.ZLinkManagedStream(socket, 'backend-rid', 'public-session'));
  const actor = await context.actors.bind({ nodeRid: 'node-a', actorId: 'actor-a', generation: 1n });

  await actor.relay({
    kind: connector.ZlinkStreamMessageKind.Send,
    codec: connector.ZlinkStreamCodec.Json,
    flags: connector.ZlinkStreamHeaderFlags.None,
    name: 'Move',
    metadata: connector.ZlinkStreamMetadataMap.empty
  }, {
    bytes: new TextEncoder().encode('{"x":1}'),
    toBytes() {
      return this.bytes;
    },
    close() {}
  });

  assert.equal(socket.boundActorSends.length, 1);
  assert.equal(socket.boundActorSends[0].sessionRid, 'backend-rid');
  assert.equal(socket.boundActorSends[0].actorId, 'actor-a');
  assert.equal(socket.boundActorSends[0].parts.length, 2);
  const header = connector.ZlinkStreamHeaderCodec.decode(socket.boundActorSends[0].parts[0].bytes);
  assert.equal(header.kind, connector.ZlinkStreamMessageKind.Send);
  assert.equal(header.name, 'Move');
  assert.equal(new TextDecoder().decode(socket.boundActorSends[0].parts[1].bytes), '{"x":1}');
});

test('bound session send and disconnect use current binding token and stale tokens cannot remove newer binding', async () => {
  const sent = [];
  const disconnected = [];
  const runtime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: binaryMessageFactory(),
    transport: {
      async send(actorId, message, options) {
        sent.push({ actorId, frame: decodeFrame(message.bytes), token: options.bindingToken, packetName: options.packetName });
      },
      async disconnect(actorId, options) {
        disconnected.push({ actorId, token: options.bindingToken });
      }
    }
  });

  const oldContext = runtime.createSessionContext(fakeStream('old-session', 'old-rid'));
  const oldActor = await oldContext.actors.bind({ nodeRid: 'node-a', actorId: 'actor-a', generation: 1 });
  const oldToken = oldActor.bindingToken;

  const newContext = runtime.createSessionContext(fakeStream('new-session', 'new-rid'));
  await newContext.actors.bind({ nodeRid: 'node-a', actorId: 'actor-a', generation: 2 });

  runtime.unbind('actor-a', oldContext, oldToken);

  await runtime.createBoundSession('actor-a').send({ hello: 'world' }).packetName('Hello').submit();
  assert.equal(sent.length, 1);
  assert.equal(sent[0].actorId, 'actor-a');
  assert.equal(sent[0].packetName, 'Hello');
  assert.equal(sent[0].frame.header.kind, connector.ZlinkStreamMessageKind.Send);
  assert.equal(sent[0].frame.header.codec, connector.ZlinkStreamCodec.Json);
  assert.equal(sent[0].frame.header.name, 'Hello');
  assert.deepEqual(JSON.parse(new TextDecoder().decode(sent[0].frame.payload)), { hello: 'world' });
  assert.equal(runtime.find('actor-a').ref.generation, 2);

  await runtime.createBoundSession('actor-a').disconnect();
  assert.equal(disconnected.length, 1);
  assert.equal(runtime.find('actor-a'), undefined);
});

test('stream binding runtime can remove actor binding during actor destroy cleanup', async () => {
  const runtime = new framework.ZLinkStreamBindingRuntime();
  const context = runtime.createSessionContext(fakeStream('session-destroy', 'rid-destroy'));
  const actor = await context.actors.bind({ nodeRid: 'node-a', actorId: 'actor-destroy', generation: 1 });

  assert.equal(runtime.find('actor-destroy'), actor);
  assert.equal(context.actors.find('actor-destroy'), actor);

  runtime.unbindActor('actor-destroy');

  assert.equal(runtime.find('actor-destroy'), undefined);
  assert.equal(context.actors.find('actor-destroy'), undefined);
  await assert.rejects(
    () => runtime.sendBoundSession('actor-destroy', { after: 'destroy' }, 'AfterDestroy', new Map()),
    { kind: framework.ZLinkFrameworkErrorKind.ActorSessionNotBound }
  );
});

test('stream session and bound session require packetName for structural payloads', async () => {
  const written = [];
  const sent = [];
  const runtime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: binaryMessageFactory(),
    transport: {
      async send(actorId, message, options) {
        sent.push({ actorId, frame: decodeFrame(message.bytes), packetName: options.packetName });
      },
      async disconnect() {}
    }
  });
  const context = runtime.createSessionContext({
    ...fakeStream('session-structural-payload', 'rid-structural-payload'),
    write(message) {
      written.push(message.bytes);
      return true;
    }
  });
  await context.actors.bind({ nodeRid: 'node-a', actorId: 'actor-structural', generation: 1 });

  await assert.rejects(
    () => context.client.send({ ok: true }).submit(),
    /Stream packetName is required when the payload type cannot provide one/
  );
  await assert.rejects(
    () => runtime.createBoundSession('actor-structural').send({ ok: true }).submit(),
    /Stream packetName is required when the payload type cannot provide one/
  );
  await context.client.send({ ok: true }).packetName('Ready').submit();
  await runtime.createBoundSession('actor-structural').send({ ok: true }).packetName('ActorReady').submit();

  assert.equal(written.length, 1);
  assert.equal(decodeFrame(written[0]).header.name, 'Ready');
  assert.equal(sent.length, 1);
  assert.equal(sent[0].packetName, 'ActorReady');
  assert.equal(sent[0].frame.header.name, 'ActorReady');
});

test('bound session without binding is a retriable framework error', async () => {
  const runtime = new framework.ZLinkStreamBindingRuntime({
    transport: {
      async send() {},
      async disconnect() {}
    }
  });

  await assert.rejects(
    () => runtime.createBoundSession('missing').send({}).submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorSessionNotBound && error.isRetriable === true
  );
});

test('session client send writes dotnet-compatible JSON stream frame through injected message factory', async () => {
  const written = [];
  const closed = [];
  const runtime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: {
      createTextMessage(payload) {
        return {
          payload,
          close() {
            closed.push(payload);
          }
        };
      },
      createBinaryMessage(payload) {
        return {
          bytes: payload,
          close() {
            closed.push(payload);
          }
        };
      }
    }
  });
  const context = runtime.createSessionContext({
    ...fakeStream('session-4', 'rid-4'),
    write(message) {
      written.push(message.bytes);
      return true;
    }
  });

  await context.client.send({ ok: true }).packetName('Ready').metadata('trace', 'send-1').submit();

  assert.equal(written.length, 1);
  const frame = decodeFrame(written[0]);
  assert.equal(frame.header.kind, connector.ZlinkStreamMessageKind.Send);
  assert.equal(frame.header.codec, connector.ZlinkStreamCodec.Json);
  assert.equal(frame.header.name, 'Ready');
  assert.equal(frame.header.metadata.get('trace'), 'send-1');
  assert.deepEqual(JSON.parse(new TextDecoder().decode(frame.payload)), { ok: true });
  assert.equal(closed.length, 1);
});

test('session client send compress writes dotnet LZ4-pickled stream payload', async () => {
  const written = [];
  const runtime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: binaryMessageFactory()
  });
  const context = runtime.createSessionContext({
    ...fakeStream('session-compress-send', 'rid-compress-send'),
    write(message) {
      written.push(message.bytes);
      return true;
    }
  });

  await context.client.send({ ok: true }).packetName('Ready').compress().submit();

  const frame = decodeFrame(written[0]);
  assert.equal((frame.header.flags & connector.ZlinkStreamHeaderFlags.PayloadCompressed) !== 0, true);
  assert.deepEqual(JSON.parse(new TextDecoder().decode(unpickleLz4(frame.payload))), { ok: true });
});

test('session client reply writes response frame only while dispatching request packet', async () => {
  const written = [];
  const runtime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: binaryMessageFactory()
  });
  const context = runtime.createSessionContext({
    ...fakeStream('session-6', 'rid-6'),
    write(message) {
      written.push(message.bytes);
      return true;
    }
  });

  await assert.rejects(
    () => context.client.reply({ ok: true }).submit(),
    /Reply is only available/
  );

  context.enterDispatch({
    kind: connector.ZlinkStreamMessageKind.Request,
    codec: connector.ZlinkStreamCodec.Json,
    flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
    requestSeq: 42n,
    name: 'Move',
    metadata: connector.ZlinkStreamMetadataMap.empty
  });
  try {
    await context.client.reply({ accepted: true }).metadata('trace', 'reply-1').submit();
  } finally {
    context.exitDispatch();
  }

  assert.equal(written.length, 1);
  const frame = decodeFrame(written[0]);
  assert.equal(frame.header.kind, connector.ZlinkStreamMessageKind.Response);
  assert.equal(frame.header.requestSeq, 42n);
  assert.equal(frame.header.name, 'Move');
  assert.equal(frame.header.metadata.get('trace'), 'reply-1');
  assert.deepEqual(JSON.parse(new TextDecoder().decode(frame.payload)), { accepted: true });
});

test('session client reply compress writes dotnet LZ4-pickled response payload', async () => {
  const written = [];
  const runtime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: binaryMessageFactory()
  });
  const context = runtime.createSessionContext({
    ...fakeStream('session-compress-reply', 'rid-compress-reply'),
    write(message) {
      written.push(message.bytes);
      return true;
    }
  });

  context.enterDispatch({
    kind: connector.ZlinkStreamMessageKind.Request,
    codec: connector.ZlinkStreamCodec.Json,
    flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
    requestSeq: 43n,
    name: 'Move',
    metadata: connector.ZlinkStreamMetadataMap.empty
  });
  try {
    await context.client.reply({ accepted: true }).compress().submit();
  } finally {
    context.exitDispatch();
  }

  const frame = decodeFrame(written[0]);
  assert.equal(frame.header.kind, connector.ZlinkStreamMessageKind.Response);
  assert.equal(frame.header.requestSeq, 43n);
  assert.equal((frame.header.flags & connector.ZlinkStreamHeaderFlags.PayloadCompressed) !== 0, true);
  assert.deepEqual(JSON.parse(new TextDecoder().decode(unpickleLz4(frame.payload))), { accepted: true });
});

test('session client send uses default binding message factory when one is not supplied', async () => {
  class ReadyPacket {}
  const runtime = new framework.ZLinkStreamBindingRuntime();
  const written = [];
  const context = runtime.createSessionContext({
    ...fakeStream('session-5', 'rid-5'),
    write(message) {
      written.push(message.data());
      return true;
    }
  });

  await context.client.send(new ReadyPacket()).submit();

  const frame = decodeFrame(written[0]);
  assert.equal(frame.header.kind, connector.ZlinkStreamMessageKind.Send);
  assert.equal(frame.header.name, 'ReadyPacket');
  assert.deepEqual(JSON.parse(new TextDecoder().decode(frame.payload)), {});
});

function fakeStream(sessionId, routingId) {
  return {
    sessionId,
    routingId,
    localAddr: undefined,
    remoteAddr: undefined,
    write() {
      return true;
    },
    async close() {}
  };
}

function binaryMessageFactory() {
  return {
    createTextMessage(payload) {
      return {
        payload,
        close() {}
      };
    },
    createBinaryMessage(payload) {
      return {
        bytes: payload,
        close() {}
      };
    }
  };
}

function decodeFrame(bytes) {
  const frame = connector.ZlinkStreamFrameCodec.decode(bytes);
  return {
    header: connector.ZlinkStreamHeaderCodec.decode(frame.header),
    payload: frame.payload
  };
}

function unpickleLz4(payload) {
  if (payload.length === 0) {
    return new Uint8Array();
  }
  assert.equal(payload[0], 0);
  return payload.slice(1);
}

class FakeStreamSocket {
  constructor() {
    this.boundActors = [];
    this.boundActorSends = [];
    this.bindError = undefined;
  }

  send() {
    return true;
  }

  disconnectPeer() {}

  async bindActor(sessionRid, actor, timeoutMs) {
    if (this.bindError !== undefined) {
      throw this.bindError;
    }
    this.boundActors.push({ sessionRid, actor, timeoutMs });
  }

  async unbindActor() {}

  sendBoundActor(sessionRid, actorId, parts, flags) {
    this.boundActorSends.push({ sessionRid, actorId, parts, flags });
    return true;
  }

  onFramedPacket() {}
  attachActorGateway() {}
  async dispose() {}
}
