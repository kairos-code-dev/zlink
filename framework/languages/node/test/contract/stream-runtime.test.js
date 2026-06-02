const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist');

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

test('bound session send and disconnect use current binding token and stale tokens cannot remove newer binding', async () => {
  const sent = [];
  const disconnected = [];
  const runtime = new framework.ZLinkStreamBindingRuntime({
    transport: {
      async send(actorId, message, options) {
        sent.push({ actorId, message, token: options.bindingToken, packetName: options.packetName });
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
  assert.equal(runtime.find('actor-a').ref.generation, 2);

  await runtime.createBoundSession('actor-a').disconnect();
  assert.equal(disconnected.length, 1);
  assert.equal(runtime.find('actor-a'), undefined);
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

test('session client send uses injected message factory instead of binding internals', async () => {
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
      }
    }
  });
  const context = runtime.createSessionContext({
    ...fakeStream('session-4', 'rid-4'),
    write(message) {
      written.push(message.payload);
      return true;
    }
  });

  await context.client.send({ ok: true }).packetName('Ready').metadata('trace', 'send-1').submit();

  assert.equal(written.length, 1);
  assert.match(written[0], /"kind":"send"/);
  assert.match(written[0], /"packetName":"Ready"/);
  assert.equal(closed.length, 1);
});

test('session client send fails retriably when message factory is not started', async () => {
  const runtime = new framework.ZLinkStreamBindingRuntime();
  const context = runtime.createSessionContext(fakeStream('session-5', 'rid-5'));

  await assert.rejects(
    () => context.client.send({}).submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected && error.isRetriable === true
  );
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
