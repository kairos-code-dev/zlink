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

test('actor client sends multipart parts to a resolved actor and returns a promise terminal', async () => {
  const sends = [];
  const node = {
    sendToActor(actor, parts) {
      sends.push({ actor, parts });
      return true;
    }
  };
  const resolver = createFailingResolver();
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: () => node,
    locationResolver: () => resolver
  });

  const submitted = client.sendToActor(actorRef(), { value: 'ping' }).packetName('ActorNotify').submit();

  assert.equal(submitted instanceof Promise, true);
  await submitted;
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
  const resolver = createFailingResolver();
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: () => node,
    locationResolver: () => resolver
  });

  const reply = await client.requestToActor(actorRef(), { value: 'ping' })
    .packetName('ActorAsk')
    .timeout(100)
    .submit();

  assert.deepEqual(reply, { value: 'pong' });
});

test('actor client request decodes a single framed handler reply through stream protocol', async () => {
  const node = {
    requestToActor(_actor, _parts, callback) {
      callback(RequestResult.Ok, createReplyFrame({ value: 'framed-pong' }));
      return true;
    }
  };
  const client = new framework.DefaultZLinkActorClient({
    nodeProvider: () => node,
    locationResolver: () => createFailingResolver()
  });

  const reply = await client.requestToActor(actorRef(), { value: 'ping' })
    .packetName('ActorAsk')
    .submit();

  assert.deepEqual(reply, { value: 'framed-pong' });
});

test('actor client maps stale ActorRef sends without resolving a replacement', async () => {
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
    () => client.sendToActor(first, { value: 'ping' }).packetName('ActorNotify').submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorLocationStale
  );

  assert.deepEqual(sends.map((actor) => actor.generation), [1n]);
});

test('actor client maps stale and disconnected route failures', async () => {
  const noNode = new framework.DefaultZLinkActorClient({
    nodeProvider: () => undefined,
    locationResolver: () => createFailingResolver()
  });
  await assert.rejects(
    () => noNode.requestToActor(actorRef('missing'), { value: 'ping' }).packetName('ActorAsk').submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected
  );

  const staleNode = {
    requestToActor(_actor, _parts, callback) {
      callback(RequestResult.Conflict, []);
      return true;
    }
  };
  const stale = new framework.DefaultZLinkActorClient({
    nodeProvider: () => staleNode,
    locationResolver: () => createFailingResolver()
  });
  await assert.rejects(
    () => stale.requestToActor(actorRef('actor-1', 1n), { value: 'ping' }).packetName('ActorAsk').submit(),
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
    locationResolver: () => createFailingResolver()
  });
  await assert.rejects(
    () => disconnected.requestToActor(actorRef(), { value: 'ping' }).packetName('ActorAsk').submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected && error.isRetriable === true
  );
});
