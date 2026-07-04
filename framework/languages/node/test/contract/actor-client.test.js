const assert = require('node:assert/strict');
const test = require('node:test');
const framework = require('../../packages/framework/dist/internal');
const { Message, RequestResult } = require('@zlink-systems/zlink');

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

function createResolver(rows) {
  const calls = [];
  return {
    calls,
    resolveActorRow(key) {
      calls.push(key.actorId);
      const next = rows.shift();
      return Promise.resolve(next === undefined ? undefined : {
        actorId: key.actorId,
        actorRef: next,
        ownerId: 'owner',
        generation: next.generation,
        updatedAt: new Date()
      });
    }
  };
}

test('actor client sends multipart parts to a resolved actor and returns a promise terminal', async () => {
  const sends = [];
  const node = {
    sendToActor(actor, parts) {
      sends.push({ actor, parts });
      return true;
    }
  };
  const resolver = createResolver([actorRef()]);
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: () => node,
    locationResolver: () => resolver
  });

  const submitted = client.sendToActor('actor-1', { value: 'ping' }).packetName('ActorNotify').submit();

  assert.equal(submitted instanceof Promise, true);
  await submitted;
  assert.deepEqual(resolver.calls, ['actor-1']);
  assert.equal(sends.length, 1);
  assert.equal(sends[0].actor.actorId, 'actor-1');
  assert.equal(sends[0].parts.length, 2);
});

test('actor client request decodes the handler reply and never auto-creates a missing actor', async () => {
  const node = {
    createActor() {
      throw new Error('actor client must not auto-create actors');
    },
    requestToActor(actor, parts, callback) {
      assert.equal(actor.actorId, 'actor-1');
      assert.equal(parts.length, 2);
      callback(RequestResult.Ok, createReplyParts({ value: 'pong' }));
      return true;
    }
  };
  const resolver = createResolver([actorRef()]);
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: () => node,
    locationResolver: () => resolver
  });

  const reply = await client.requestToActor('actor-1', { value: 'ping' })
    .packetName('ActorAsk')
    .timeout(100)
    .submit();

  assert.deepEqual(reply, { value: 'pong' });
});

test('actor client retries stale actor locations once and uses the refreshed ref', async () => {
  const first = actorRef('actor-1', 1n);
  const second = actorRef('actor-1', 2n);
  const sends = [];
  const node = {
    sendToActor(actor) {
      sends.push(actor);
      if (sends.length === 1) {
        throw new framework.ZLinkFrameworkException(
          framework.ZLinkFrameworkErrorKind.ActorLocationStale,
          'stale'
        );
      }
      return true;
    }
  };
  const resolver = createResolver([first, second]);
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: () => node,
    locationResolver: () => resolver
  });

  await client.sendToActor('actor-1', { value: 'ping' }).packetName('ActorNotify').submit();

  assert.deepEqual(resolver.calls, ['actor-1', 'actor-1']);
  assert.deepEqual(sends.map((actor) => actor.generation), [1n, 2n]);
});

test('actor client maps missing, stale-after-reresolve, and disconnected route failures', async () => {
  const missing = new framework.DefaultZLinkActorClient({
    nodeProvider: () => ({}),
    locationResolver: () => createResolver([])
  });
  await assert.rejects(
    () => missing.requestToActor('missing', { value: 'ping' }).packetName('ActorAsk').submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorRouteNotFound
  );

  const staleNode = {
    requestToActor(_actor, _parts, callback) {
      callback(RequestResult.Conflict, []);
      return true;
    }
  };
  const stale = new framework.DefaultZLinkActorClient({
    nodeProvider: () => staleNode,
    locationResolver: () => createResolver([actorRef('actor-1', 1n), actorRef('actor-1', 2n)])
  });
  await assert.rejects(
    () => stale.requestToActor('actor-1', { value: 'ping' }).packetName('ActorAsk').submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorLocationStale
  );

  const disconnectedNode = {
    requestToActor(_actor, _parts, callback) {
      callback(RequestResult.NotConnected, []);
      return true;
    }
  };
  const disconnected = new framework.DefaultZLinkActorClient({
    nodeProvider: () => disconnectedNode,
    locationResolver: () => createResolver([actorRef()])
  });
  await assert.rejects(
    () => disconnected.requestToActor('actor-1', { value: 'ping' }).packetName('ActorAsk').submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected && error.isRetriable === true
  );
});
