const assert = require('node:assert/strict');
const test = require('node:test');

const connector = require('../../packages/stream-connector/dist');
const framework = require('../../packages/framework/dist/internal');
const streamProtocol = require('../../packages/framework/dist/runtime/streams/protocol');
const zlink = require('@zlink-systems/zlink');

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

test('managed stream remote actor bind records the remote actor ref on the stream', async () => {
  const socket = new FakeStreamSocket();
  const node = new FakeSpotNode('node-local');
  const runtime = new framework.ZLinkStreamBindingRuntime({
    actorBindTimeoutMs: 1234,
    nativeActorNodeProvider: () => node
  });
  const context = runtime.createSessionContext(new framework.ZLinkManagedStream(socket, 'backend-rid', 'public-session'));
  const actorRef = { nodeRid: 'node-remote', actorId: 'actor-a', generation: 1n };

  const actor = await context.actors.bind(actorRef);

  assert.equal(actor.actorId, 'actor-a');
  assert.equal(socket.boundActors.length, 1);
  assert.deepEqual(socket.boundActors[0], {
    sessionRid: 'backend-rid',
    actor: actorRef,
    timeoutMs: 1234
  });
  assert.deepEqual(node.remoteSessionBinds, []);
  assert.equal(context.actors.find('actor-a'), actor);
});

test('managed stream actor rebind is idempotent for the same actor ref', async () => {
  const socket = new FakeStreamSocket();
  const runtime = new framework.ZLinkStreamBindingRuntime({ actorBindTimeoutMs: 1234 });
  const context = runtime.createSessionContext(new framework.ZLinkManagedStream(socket, 'backend-rid', 'public-session'));
  const actorRef = { nodeRid: 'node-a', actorId: 'actor-a', generation: 1n };

  await context.actors.bind(actorRef);
  await runtime.rebindActor(actorRef);

  assert.equal(socket.boundActors.length, 1);
  assert.deepEqual(socket.boundActors[0], {
    sessionRid: 'backend-rid',
    actor: actorRef,
    timeoutMs: 1234
  });
});

test('managed stream actor refresh rebinds native gateway for the same actor ref', async () => {
  const socket = new FakeStreamSocket();
  const runtime = new framework.ZLinkStreamBindingRuntime({ actorBindTimeoutMs: 1234 });
  const context = runtime.createSessionContext(new framework.ZLinkManagedStream(socket, 'backend-rid', 'public-session'));
  const actorRef = { nodeRid: 'node-a', actorId: 'actor-a', generation: 1n };

  await context.actors.bind(actorRef);
  await runtime.refreshActor(actorRef);

  assert.equal(socket.boundActors.length, 2);
  assert.deepEqual(socket.boundActors[1], {
    sessionRid: 'backend-rid',
    actor: actorRef,
    timeoutMs: 1234
  });
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

test('runtime host bound session uses local stream route before native ActorGateway', async () => {
  const actorRef = { nodeRid: 'node-a', actorId: 'actor-native', generation: 7n };
  const nativeSends = [];
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  host.spotNodeRuntime = {
    primaryNode: {
      sendActorBoundSession(actor, parts, flags) {
        nativeSends.push({ actor, frame: decodeFrame(bytesOf(parts[0])), flags });
        return true;
      },
      async closeActorBoundSession() {}
    }
  };

  const stream = recordingStream('session-native', 'rid-native');
  const context = host.streamBindingRuntime.createSessionContext(stream);
  await context.actors.bind(actorRef);

  const submit = host.createActorManagerOptions()
    .boundSessionFactory(actorRef.actorId)
    .send({ ok: true })
    .packetName('Notify')
    .submit();

  assert.equal(nativeSends.length, 0);
  assert.equal(stream.writes.length, 1);
  await submit;
  assert.equal(stream.writes.length, 1);
  const frame = decodeFrame(bytesOf(stream.writes[0]));
  assert.equal(frame.header.kind, connector.ZlinkStreamMessageKind.Send);
  assert.equal(frame.header.name, 'Notify');
  assert.deepEqual(JSON.parse(new TextDecoder().decode(frame.payload)), { ok: true });
});

test('runtime host bound session falls back to native ActorGateway when no local route exists', async () => {
  const actorRef = { nodeRid: 'node-a', actorId: 'actor-native', generation: 7n };
  const nativeSends = [];
  const routeCalls = [];
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  host.routeTransport.send = (routerChannelId, targetNodeRid, packetName, message, signal) => {
    routeCalls.push({ routerChannelId, targetNodeRid, packetName, message, signal });
    return Promise.resolve({ ok: true });
  };
  host.spotNodeRuntime = {
    primaryNode: {
      sendActorBoundSession(actor, parts, flags) {
        nativeSends.push({ actor, frame: decodeFrame(bytesOf(parts[0])), flags });
        return true;
      },
      async closeActorBoundSession() {}
    }
  };
  host.setActorManager({
    getState(actorId) {
      return actorId === actorRef.actorId
        ? { nativeActorRef: actorRef }
        : undefined;
    }
  });

  const submit = host.createActorManagerOptions()
    .boundSessionFactory(actorRef.actorId)
    .send({ ok: true })
    .packetName('Notify')
    .submit();

  assert.equal(nativeSends.length, 0);
  await submit;
  assert.equal(nativeSends.length, 1);
  assert.equal(routeCalls.length, 0);
  assert.deepEqual(nativeSends[0].actor, { ...actorRef, generation: 0n });
  assert.equal(nativeSends[0].flags, 1);
  assert.equal(nativeSends[0].frame.header.kind, connector.ZlinkStreamMessageKind.Send);
  assert.equal(nativeSends[0].frame.header.name, 'Notify');
  assert.deepEqual(JSON.parse(new TextDecoder().decode(nativeSends[0].frame.payload)), { ok: true });
});

test('runtime host bound session uses routed Session target before native ActorGateway', async () => {
  const actorRef = { nodeRid: 'node-a', actorId: 'actor-routed', generation: 7n };
  const nativeSends = [];
  const routeCalls = [];
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  host.routeTransport.send = (routerChannelId, targetNodeRid, packetName, message, signal) => {
    routeCalls.push({ routerChannelId, targetNodeRid, packetName, message, signal });
    return Promise.resolve();
  };
  host.spotNodeRuntime = {
    primaryNode: {
      sendActorBoundSession(actor, parts, flags) {
        nativeSends.push({ actor, parts, flags });
        return true;
      },
      async closeActorBoundSession() {}
    }
  };
  host.setActorManager({
    getState(actorId) {
      return actorId === actorRef.actorId
        ? {
            nativeActorRef: actorRef,
            remoteBoundSessionTarget: {
              routerChannelId: 'room.route',
              targetNodeRid: 'session-node',
              spotRid: 'session-entry'
            }
          }
        : undefined;
    }
  });

  await host.createActorManagerOptions()
    .boundSessionFactory(actorRef.actorId)
    .send({ ok: true })
    .packetName('Notify')
    .submit();

  assert.equal(nativeSends.length, 0);
  assert.equal(routeCalls.length, 1);
  assert.equal(routeCalls[0].routerChannelId, 'room.route');
  assert.equal(routeCalls[0].targetNodeRid, 'session-node');
  assert.equal(routeCalls[0].packetName, '__zlink.actor.bound_session.send');
  assert.equal(routeCalls[0].message.boundPacketName, 'Notify');
});

test('runtime host native bound session retries while ActorGateway route is connecting', async () => {
  const actorRef = { nodeRid: 'node-a', actorId: 'actor-native', generation: 7n };
  const attempts = [];
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  host.streamBindingRuntime = new framework.ZLinkStreamBindingRuntime({ actorBindTimeoutMs: 100 });
  host.spotNodeRuntime = {
    primaryNode: {
      sendActorBoundSession(actor, parts, flags) {
        attempts.push({ actor, frame: decodeFrame(bytesOf(parts[0])), flags });
        if (attempts.length === 1) {
          throw new zlink.SubmitError(zlink.SubmitResult.NotConnected);
        }
        if (attempts.length === 2) {
          return false;
        }
        return true;
      },
      async closeActorBoundSession() {}
    }
  };
  host.setActorManager({
    getState(actorId) {
      return actorId === actorRef.actorId ? { nativeActorRef: actorRef } : undefined;
    }
  });

  await host.createActorManagerOptions()
    .boundSessionFactory(actorRef.actorId)
    .send({ ok: true })
    .packetName('Notify')
    .submit();

  assert.equal(attempts.length, 3);
  assert.deepEqual(attempts.map((entry) => entry.actor), [
    { ...actorRef, generation: 0n },
    { ...actorRef, generation: 0n },
    { ...actorRef, generation: 0n }
  ]);
  assert.deepEqual(attempts.map((entry) => entry.flags), [1, 1, 1]);
  assert.equal(attempts[2].frame.header.name, 'Notify');
  assert.deepEqual(JSON.parse(new TextDecoder().decode(attempts[2].frame.payload)), { ok: true });
});

test('runtime host remote bound session send submits a routed Session command', async () => {
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  const routeCalls = [];
  host.routeTransport.sendToSpot = (remoteAddress, message, options) => {
    routeCalls.push({
      routerChannelId: remoteAddress.routerChannelId,
      targetNodeRid: remoteAddress.targetNodeRid,
      spotRid: remoteAddress.spotRid,
      packetName: options.packetName,
      packet: message,
      signal: options.signal
    });
    return Promise.resolve();
  };
  host.setActorManager({
    getState(actorId) {
      assert.equal(actorId, 'actor-remote');
      return {
        remoteBoundSessionTarget: {
          routerChannelId: 'room.route',
          targetNodeRid: 'session-node',
          spotRid: 'session-entry'
        }
      };
    }
  });

  await host.createActorManagerOptions()
    .boundSessionFactory('actor-remote')
    .send({ hello: 'world' })
    .packetName('Notify')
    .submit();

  assert.equal(routeCalls.length, 1);
  assert.equal(routeCalls[0].routerChannelId, 'room.route');
  assert.equal(routeCalls[0].targetNodeRid, 'session-node');
  assert.equal(routeCalls[0].spotRid, 'session-entry');
  assert.equal(routeCalls[0].packetName, '__zlink.actor.bound_session.send');
  assert.equal(routeCalls[0].packet.packetName, '__zlink.actor.bound_session.send');
  assert.equal(routeCalls[0].packet.actorId, 'actor-remote');
  assert.equal(routeCalls[0].packet.boundPacketName, 'Notify');
});

test('runtime host remote bound session receiver forwards through actor remote target', async () => {
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  const routeCalls = [];
  host.routeTransport.sendToSpot = (remoteAddress, message, options) => {
    routeCalls.push({
      routerChannelId: remoteAddress.routerChannelId,
      targetNodeRid: remoteAddress.targetNodeRid,
      spotRid: remoteAddress.spotRid,
      packetName: options.packetName,
      packet: message,
      signal: options.signal
    });
    return Promise.resolve();
  };
  host.setActorManager({
    getState(actorId) {
      assert.equal(actorId, 'actor-hop');
      return {
        remoteBoundSessionTarget: {
          routerChannelId: 'room.route',
          targetNodeRid: 'session-node',
          spotRid: 'session-entry'
        }
      };
    }
  });

  await host.receiveRemoteBoundSessionSend({
    packetName: '__zlink.actor.bound_session.send',
    actorId: 'actor-hop',
    message: { hello: 'world' },
    boundPacketName: 'Notify',
    metadata: { seq: '1' }
  });

  assert.equal(routeCalls.length, 1);
  assert.equal(routeCalls[0].routerChannelId, 'room.route');
  assert.equal(routeCalls[0].targetNodeRid, 'session-node');
  assert.equal(routeCalls[0].spotRid, 'session-entry');
  assert.equal(routeCalls[0].packetName, '__zlink.actor.bound_session.send');
  assert.equal(routeCalls[0].packet.actorId, 'actor-hop');
  assert.equal(routeCalls[0].packet.boundPacketName, 'Notify');
  assert.deepEqual(routeCalls[0].packet.message, { hello: 'world' });
  assert.deepEqual(routeCalls[0].packet.metadata, { seq: '1' });
});

test('runtime host actor packet target prefers stored remote room target', () => {
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  host.spotNodeRuntime = {
    primaryNode: {
      routingId: 'local-node'
    }
  };
  host.setActorManager({
    getState(actorId) {
      assert.equal(actorId, 'actor-remote-room');
      return {
        spotRid: 'room-spot',
        nativeActorRef: {
          nodeRid: 'actor-home-node',
          actorId,
          generation: 1n
        },
        remoteActorPacketTarget: {
          routerChannelId: 'room.route',
          targetNodeRid: 'room-owner-node',
          spotRid: 'room-spot'
        }
      };
    }
  });

  assert.deepEqual(host.actorPacketTargetForState('actor-remote-room'), {
    routerChannelId: 'room.route',
    targetNodeRid: 'room-owner-node',
    spotRid: 'room-spot'
  });
});

test('runtime host actor packet target lets local joined actors use native gateway', () => {
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  host.spotNodeRuntime = {
    primaryNode: {
      routingId: 'local-node'
    }
  };
  host.setActorManager({
    getState(actorId) {
      assert.equal(actorId, 'actor-local-room');
      return {
        spotRid: 'room-spot',
        nativeActorRef: {
          nodeRid: 'local-node',
          actorId,
          generation: 1n
        }
      };
    }
  });

  assert.equal(host.actorPacketTargetForState('actor-local-room'), undefined);
});

test('runtime host local spot join uses SpotManager for actors with native refs', async () => {
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  host.spotNodeRuntime = {
    primaryNode: {
      routingId: 'local-node'
    }
  };
  const actor = { actorId: 'actor-local-room' };
  const state = new framework.ZLinkActorRuntimeState(actor.actorId);
  state.setNativeActorRef({
    nodeRid: 'local-node',
    actorId: actor.actorId,
    generation: 4n
  });
  const admits = [];
  host.setSpotManager({
    hasActiveSpot(spotRid) {
      return spotRid === 'room-1';
    },
    async admitActorJoin(spotRid, joinedActor, request, commit) {
      admits.push({ spotRid, actorId: joinedActor.actorId, request: request.getString() });
      await commit({ room: true });
      return { accepted: true, reply: zlink.Message.from('joined') };
    }
  });

  const request = zlink.Message.from('hello');
  const result = await host.createActorManagerOptions()
    .joinCoordinator
    .joinSpot(actor, state, 'room-1', request, undefined, undefined);

  assert.deepEqual(admits, [{ spotRid: 'room-1', actorId: 'actor-local-room', request: 'hello' }]);
  assert.equal(state.spotRid, 'room-1');
  assert.deepEqual(result.actor, {
    nodeRid: 'local-node',
    actorId: 'actor-local-room',
    generation: 4n
  });
  assert.equal(result.reply.getString(), 'joined');
  request.close();
  result.reply.close();
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

test('runtime host relays bound remote actor request through route channel and completes local stream response', async () => {
  const actorRef = { nodeRid: 'play-node', actorId: 'actor-remote', generation: 7n };
  const routeRequests = [];
  const stream = recordingStream('session-remote-actor', 'session-node');
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      routeChannels: [{ routerChannelId: 'room.route' }],
      spotNodes: {
        session: {
          router: { bind: 'tcp://127.0.0.1:1', routingId: 'session-node' },
          acceptedSpotRouteChannels: {
            'room.route': {}
          }
        }
      }
    })
  });
  host.spotNodeRuntime = {
    primaryNode: {
      routingId: 'session-node'
    }
  };
  host.routeTransport.requestRawToSpot = async (remoteAddress, request, options) => {
    const payload = JSON.parse(request.data().toString());
    routeRequests.push({
      routerChannelId: remoteAddress.routerChannelId,
      targetNodeRid: remoteAddress.targetNodeRid,
      spotRid: remoteAddress.spotRid,
      packetName: payload.packetName,
      timeoutMs: options.timeoutMs,
      request: payload
    });
    return [zlink.Message.from(JSON.stringify({
      ok: true,
      response: { matched: true }
    }))];
  };

  const context = host.streamBindingRuntime.createSessionContext(stream);
  const actor = await context.actors.bind(actorRef);
  await actor.relay({
    kind: connector.ZlinkStreamMessageKind.Request,
    codec: connector.ZlinkStreamCodec.Json,
    flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
    requestSeq: 2n,
    name: 'MatchBingoReq',
    metadata: connector.ZlinkStreamMetadataMap.empty
  }, bindingMessage(JSON.stringify({ mode: 'classic' })));

  assert.equal(routeRequests.length, 1);
  assert.equal(routeRequests[0].routerChannelId, 'room.route');
  assert.equal(routeRequests[0].targetNodeRid, 'play-node');
  assert.equal(routeRequests[0].spotRid, 'play-node');
  assert.equal(routeRequests[0].packetName, '__zlink.actor.packet.relay');
  assert.equal(routeRequests[0].request.packetName, '__zlink.actor.packet.relay');
  assert.equal(routeRequests[0].request.actorId, 'actor-remote');
  assert.equal(stream.writes.length, 1);
  const frame = decodeFrame(bytesOf(stream.writes[0]));
  assert.equal(frame.header.kind, connector.ZlinkStreamMessageKind.Response);
  assert.equal(frame.header.name, 'MatchBingoReq');
  assert.equal(frame.header.requestSeq, 2n);
  assert.deepEqual(JSON.parse(new TextDecoder().decode(frame.payload)), { matched: true });
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

test('local bound session error response rejects pending actor request', async () => {
  const stream = recordingStream('session-error-response', 'rid-error-response');
  const runtime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: binaryMessageFactory()
  });
  const context = runtime.createSessionContext(stream);
  await context.actors.bind({ nodeRid: 'node-a', actorId: 'actor-error', generation: 1 });
  const pending = context.startRequest(1000);

  assert.equal(runtime.sendLocalBoundSessionError(
    'actor-error',
    'Move',
    pending.requestSeq,
    new Error('remote actor failed'),
    new Map()
  ), true);

  const frame = decodeFrame(stream.writes[0].bytes);
  assert.equal(frame.header.kind, connector.ZlinkStreamMessageKind.Error);
  const header = {
    kind: streamProtocol.ZLinkStreamMessageKind.Error,
    codec: streamProtocol.ZLinkStreamCodec.Json,
    flags: streamProtocol.ZLinkStreamHeaderFlags.HasRequestSeq,
    requestSeq: pending.requestSeq,
    name: 'Move',
    metadata: { values: new Map() }
  };
  const payload = {
    data: () => Buffer.from(frame.payload),
    close() {}
  };

  assert.equal(context.tryCompleteResponse(header, payload), true);
  await assert.rejects(
    () => pending.promise,
    /remote actor failed/
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

test('session client reply uses configured stream payload codec', async () => {
  const written = [];
  const runtime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: binaryMessageFactory(),
    streamPayloadCodec: {
      encode(payload) {
        return {
          codec: connector.ZlinkStreamCodec.Protobuf,
          payload: new TextEncoder().encode(`proto:${payload.accepted}`)
        };
      }
    }
  });
  const context = runtime.createSessionContext({
    ...fakeStream('session-protobuf-reply', 'rid-protobuf-reply'),
    write(message) {
      written.push(message.bytes);
      return true;
    }
  });

  context.enterDispatch({
    kind: connector.ZlinkStreamMessageKind.Request,
    codec: connector.ZlinkStreamCodec.Protobuf,
    flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
    requestSeq: 44n,
    name: 'AuthenticateReq',
    metadata: connector.ZlinkStreamMetadataMap.empty
  });
  try {
    await context.client.reply({ accepted: true }).submit();
  } finally {
    context.exitDispatch();
  }

  assert.equal(written.length, 1);
  const frame = decodeFrame(written[0]);
  assert.equal(frame.header.kind, connector.ZlinkStreamMessageKind.Response);
  assert.equal(frame.header.codec, connector.ZlinkStreamCodec.Protobuf);
  assert.equal(frame.header.requestSeq, 44n);
  assert.equal(frame.header.name, 'AuthenticateReq');
  assert.equal(new TextDecoder().decode(frame.payload), 'proto:true');
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

function recordingStream(sessionId, routingId) {
  const writes = [];
  return {
    sessionId,
    routingId,
    localAddr: undefined,
    remoteAddr: undefined,
    writes,
    write(message) {
      writes.push(message);
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

function bytesOf(message) {
  if (message.toBytes !== undefined) {
    return message.toBytes();
  }
  if (message.data !== undefined) {
    return message.data();
  }
  if (message.bytes !== undefined) {
    return message.bytes;
  }
  throw new Error('Test message does not expose bytes.');
}

function bindingMessage(payload) {
  const bytes = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  return {
    data() {
      return bytes;
    },
    toBytes() {
      return bytes;
    },
    close() {}
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

class FakeSpotNode {
  constructor(routingId) {
    this.routingId = routingId;
    this.remoteSessionBinds = [];
  }

  bindRemoteActorSession(actor, sourceNodeRid, sourceSessionRid) {
    this.remoteSessionBinds.push({ actor, sourceNodeRid, sourceSessionRid });
  }
}
