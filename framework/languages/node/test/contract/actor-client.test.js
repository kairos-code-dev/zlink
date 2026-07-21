const assert = require('node:assert/strict');
const test = require('node:test');
const framework = require('../../packages/framework/dist/internal');
const { Message, RequestResult } = require('@zlink-systems/zlink');

class ActorNotify { constructor(value) { this.value = value; } }
class ActorAsk { constructor(value) { this.value = value; } }

function actorRef(actorId = 'actor-1', generation = 1n) {
  return { nodeRid: 'node-a', actorId, generation };
}

function createReplyParts(value) {
  return [
    Message.from(Buffer.from(framework.encodeStreamHeader({
      kind: framework.ZLinkStreamMessageKind.Response,
      codec: framework.ZLinkStreamCodec.Json,
      flags: framework.ZLinkStreamHeaderFlags.None,
      name: 'ActorReply',
      metadata: new Map()
    }))),
    Message.from(Buffer.from(JSON.stringify(value)))
  ];
}

function createReplyFrame(value) {
  return [
    Message.from(Buffer.from(framework.encodeStreamFrame({
      kind: framework.ZLinkStreamMessageKind.Response,
      codec: framework.ZLinkStreamCodec.Json,
      flags: framework.ZLinkStreamHeaderFlags.None,
      name: 'ActorReply',
      metadata: new Map()
    }, Buffer.from(JSON.stringify(value)))))
  ];
}

function createFailingResolver() {
  return {
    resolveActorRow() {
      throw new Error('actor client must not resolve ActorRef targets');
    }
  };
}

const operationId = Object.freeze({ high: 1n, low: 2n });

function completionTable(terminalResult, parts = []) {
  return {
    async wait(actualOperationId) {
      assert.deepEqual(actualOperationId, operationId);
      return {
        terminalResult,
        failureErrno: 0,
        operationKind: 0,
        kindData: null,
        parts
      };
    }
  };
}

test('actor client submit returns the formal one-way admission result', async () => {
  const sends = [];
  const node = {
    sendToActor(actor, parts) {
      sends.push({ actor, parts });
      return 0;
    }
  };
  const resolver = createFailingResolver();
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: () => node,
    locationResolver: () => resolver
  });

  const submitted = await client.sendToActor(
    'play-mesh',
    actorRef(),
    new ActorNotify('ping')
  ).submit();

  assert.deepEqual(submitted, { status: framework.ZLinkSubmitStatus.Submitted });
  assert.equal(sends.length, 1);
  assert.equal(sends[0].actor.actorId, 'actor-1');
  assert.equal(sends[0].parts.length, 2);
});

test('actor client selects the MeshNode and completion table by explicit RouteMesh name', async () => {
  const selected = [];
  const nodes = new Map([
    ['mesh-a', {
      requestToActor() {
        selected.push('node:mesh-a');
        return operationId;
      }
    }],
    ['mesh-b', {
      requestToActor() {
        selected.push('node:mesh-b');
        return operationId;
      }
    }]
  ]);
  const completions = new Map([
    ['mesh-a', {
      async wait() {
        selected.push('completion:mesh-a');
        return {
          terminalResult: RequestResult.Ok,
          failureErrno: 0,
          operationKind: 0,
          kindData: null,
          parts: createReplyParts({ mesh: 'mesh-a' })
        };
      }
    }],
    ['mesh-b', {
      async wait() {
        selected.push('completion:mesh-b');
        return {
          terminalResult: RequestResult.Ok,
          failureErrno: 0,
          operationKind: 0,
          kindData: null,
          parts: createReplyParts({ mesh: 'mesh-b' })
        };
      }
    }]
  ]);
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: (meshName) => nodes.get(meshName),
    completionTableProvider: (meshName) => completions.get(meshName),
    locationResolver: () => createFailingResolver()
  });

  const first = await client
    .requestToActor('mesh-a', actorRef(), new ActorAsk('ping'))
    .submit();
  const second = await client
    .requestToActor('mesh-b', actorRef(), new ActorAsk('ping'))
    .submit();

  assert.deepEqual(first, { mesh: 'mesh-a' });
  assert.deepEqual(second, { mesh: 'mesh-b' });
  assert.deepEqual(selected, [
    'node:mesh-a',
    'completion:mesh-a',
    'node:mesh-b',
    'completion:mesh-b'
  ]);
  assert.throws(
    () => client.requestToActor(' mesh-a', actorRef(), new ActorAsk('ping')),
    /RouteMesh name must not be empty or padded/
  );
});

test('actor client request decodes the handler reply and never auto-creates a missing actor', async () => {
  const node = {
    createActor() {
      throw new Error('actor client must not auto-create actors');
    },
    requestToActor(actor, parts, options) {
      assert.equal(actor.actorId, 'actor-1');
      assert.equal(parts.length, 2);
      assert.equal(options.timeoutMs, 100);
      return operationId;
    }
  };
  const completions = completionTable(
    RequestResult.Ok,
    createReplyParts({ value: 'pong' })
  );
  const resolver = createFailingResolver();
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: () => node,
    completionTableProvider: () => completions,
    locationResolver: () => resolver
  });

  const reply = await client.requestToActor('play-mesh', actorRef(), new ActorAsk('ping'))
    .timeout(100)
    .submit();

  assert.deepEqual(reply, { value: 'pong' });
});

test('actor client request decodes a single framed handler reply through stream protocol', async () => {
  const node = {
    requestToActor() {
      return operationId;
    }
  };
  const completions = completionTable(
    RequestResult.Ok,
    createReplyFrame({ value: 'framed-pong' })
  );
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: () => node,
    completionTableProvider: () => completions,
    locationResolver: () => createFailingResolver()
  });

  const reply = await client.requestToActor('play-mesh', actorRef(), new ActorAsk('ping'))
    .submit();

  assert.deepEqual(reply, { value: 'framed-pong' });
});

test('actor client maps stale ActorRef submit without resolving a replacement', async () => {
  const first = actorRef('actor-1', 1n);
  const sends = [];
  const node = {
    sendToActor(actor) {
      sends.push(actor);
      throw new framework.ZLinkFrameworkException(
        framework.ZLinkFrameworkErrorKind.ActorLocationStale,
        'stale'
      );
    }
  };
  const resolver = createFailingResolver();
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: () => node,
    locationResolver: () => resolver
  });

  await assert.rejects(
    () => client.sendToActor('play-mesh', first, new ActorNotify('ping')).submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorLocationStale
  );

  assert.deepEqual(sends.map((actor) => actor.generation), [1n]);
});

test('actor client submit maps native admission statuses without hiding invalid failures', async () => {
  const results = [
    [0, framework.ZLinkSubmitStatus.Submitted],
    [2, framework.ZLinkSubmitStatus.RouteNotConnected],
    [3, framework.ZLinkSubmitStatus.TargetNotFound],
    [4, framework.ZLinkSubmitStatus.Shutdown]
  ];
  for (const [nativeResult, expected] of results) {
    const client = new framework.DefaultZLinkActorClient({
      nodeProvider: () => ({
        sendToActor() {
          return nativeResult;
        }
      }),
      completionTableProvider: () => undefined,
      locationResolver: () => createFailingResolver(),
      meshSubmitters: new framework.ZLinkMeshSubmitterRegistry(5)
    });
    assert.deepEqual(
      await client.sendToActor('play-mesh', actorRef(), new ActorNotify('ping')).submit(),
      { status: expected }
    );
  }

  for (const nativeResult of [1, 13]) {
    const client = new framework.DefaultZLinkActorClient({
      nodeProvider: () => ({ sendToActor: () => nativeResult }),
      completionTableProvider: () => undefined,
      locationResolver: () => createFailingResolver(),
      meshSubmitters: new framework.ZLinkMeshSubmitterRegistry(5)
    });
    assert.deepEqual(
      await client.sendToActor('play-mesh', actorRef(), new ActorNotify('ping')).submit(),
      { status: framework.ZLinkSubmitStatus.TimedOut }
    );
  }

  const invalid = new framework.DefaultZLinkActorClient({
    nodeProvider: () => ({
      sendToActor() {
        return 6;
      }
    }),
    completionTableProvider: () => undefined,
    locationResolver: () => createFailingResolver(),
    meshSubmitters: new framework.ZLinkMeshSubmitterRegistry(5)
  });
  await assert.rejects(
    () => invalid.sendToActor('play-mesh', actorRef(), new ActorNotify('ping')).submit(),
    /submit result 6/
  );
});

test('Mesh submit cancellation removes pending admission and prevents late replay', async () => {
  let attempts = 0;
  const registry = new framework.ZLinkMeshSubmitterRegistry(1000);
  const controller = new AbortController();
  const pending = registry.submit('play-mesh', () => {
    attempts += 1;
    return { status: framework.ZLinkSubmitStatus.Backpressured };
  }, controller.signal);
  controller.abort();
  await assert.rejects(pending, (error) => error?.name === 'AbortError');
  registry.notify('play-mesh');

  assert.equal(attempts, 1);
  registry.dispose();
});

test('Mesh submit shutdown rejects pending and future admission without late replay', async () => {
  let attempts = 0;
  const registry = new framework.ZLinkMeshSubmitterRegistry(1000);
  const pending = registry.submit('play-mesh', () => {
    attempts += 1;
    return { status: framework.ZLinkSubmitStatus.Backpressured };
  });

  registry.dispose();
  assert.deepEqual(
    await pending,
    { status: framework.ZLinkSubmitStatus.Shutdown }
  );
  registry.notify('play-mesh');
  assert.equal(attempts, 1);

  assert.deepEqual(
    await registry.submit('play-mesh', () => {
      attempts += 1;
      return { status: framework.ZLinkSubmitStatus.Submitted };
    }),
    { status: framework.ZLinkSubmitStatus.Shutdown }
  );
  assert.equal(attempts, 1);
});

test('actor target validation wins over a pre-aborted signal', async () => {
  const controller = new AbortController();
  controller.abort();
  let attempts = 0;
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: () => ({
      sendToActor() {
        attempts += 1;
        return zlink.SubmitResult.Ok;
      }
    }),
    completionTableProvider: () => undefined,
    locationResolver: () => createFailingResolver(),
    staleActorRefPredicate: () => true
  });

  await assert.rejects(
    () => client.sendToActor('play-mesh', actorRef(), new ActorNotify('ping')).submit(controller.signal),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorLocationStale
  );
  assert.equal(attempts, 0);
});

test('actor client maps stale and disconnected route failures', async () => {
  const noNode = new framework.DefaultZLinkActorClient({
    nodeProvider: () => undefined,
    completionTableProvider: () => undefined,
    locationResolver: () => createFailingResolver()
  });
  await assert.rejects(
    () => noNode.requestToActor('play-mesh', actorRef('missing'), new ActorAsk('ping')).submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected
  );

  const staleNode = {
    requestToActor() {
      return operationId;
    }
  };
  const stale = new framework.DefaultZLinkActorClient({
    nodeProvider: () => staleNode,
    completionTableProvider: () => completionTable(RequestResult.Conflict),
    locationResolver: () => createFailingResolver()
  });
  await assert.rejects(
    () => stale.requestToActor('play-mesh', actorRef('actor-1', 1n), new ActorAsk('ping')).submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorLocationStale
  );

  const disconnectedNode = {
    requestToActor() {
      return operationId;
    }
  };
  const disconnected = new framework.DefaultZLinkActorClient({
    nodeProvider: () => disconnectedNode,
    completionTableProvider: () => completionTable(RequestResult.NotConnected),
    locationResolver: () => createFailingResolver()
  });
  await assert.rejects(
    () => disconnected.requestToActor('play-mesh', actorRef(), new ActorAsk('ping')).submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected && error.isRetriable === true
  );
});

test('actor client preserves ActorRouteNotFound for a missing actor route', async () => {
  const missingNode = {
    requestToActor() {
      return operationId;
    }
  };
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: () => missingNode,
    completionTableProvider: () => completionTable(RequestResult.NotFound),
    locationResolver: () => createFailingResolver()
  });

  await assert.rejects(
    () => client.requestToActor('play-mesh', actorRef('missing-actor'), new ActorAsk('ping')).submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorRouteNotFound
  );
});
