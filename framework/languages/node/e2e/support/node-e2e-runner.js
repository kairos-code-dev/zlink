#!/usr/bin/env node
const assert = require('node:assert/strict');
const fs = require('node:fs');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');

const nodeRoot = path.resolve(__dirname, '../..');

const suites = new Map([
  ['RegistryMessaging', registryMessaging],
  ['PubSub', pubSub],
  ['RegistrationCodec', registrationCodec],
  ['Monitoring', monitoring],
  ['DiscoveryRegistryHa', discoveryRegistryHa],
  ['ResilienceLifecycle', resilienceLifecycle],
  ['SpotService', spotService]
]);

async function main() {
  const suiteName = process.argv[2];
  const suite = suites.get(suiteName);
  if (suite === undefined) {
    throw new Error(`Unknown Node e2e suite: ${suiteName}`);
  }

  await suite();
}

async function registryMessaging() {
  const apiEndpoint = uniqueEndpoint('rm-api');
  const nestjs = loadNest();
  const { app } = await startApp(
    nestModule('RegistryMessagingModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .addClientServerChannel('rm.api')
            .enableServer(apiEndpoint)
            .enableClient(apiEndpoint)
            .routingId('rm-provider-a')
            .addRequestHandler('rm.echo', EchoHandler)
            .addSendHandler('rm.audit', AuditHandler)
          .build())
      ],
      providers: [EchoHandler, AuditHandler]
    })
  );

  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const reply = await client
      .requestToChannel('rm.api', { id: 7, text: 'hello' })
      .packetName('rm.echo')
      .timeout(3000)
      .submit();
    assert.deepEqual(reply, { id: 7, text: 'hello', handledBy: 'rm-provider-a' });

    await client
      .sendToChannel('rm.api', { id: 8, text: 'audit' })
      .packetName('rm.audit')
      .submit();
    await waitFor(() => AuditHandler.messages.some((message) => message.id === 8));
    selfCheck('RM-MANUAL-REQUEST-SEND');
    marker('RM-A2');
  } finally {
    await app.close();
  }
}

async function pubSub() {
  PubSubAlphaHandler.events = [];
  PubSubBetaHandler.events = [];
  PubSubGammaHandler.events = [];
  PubSubLateHandler.events = [];
  PubSubSlowHandler.started = [];
  PubSubSlowHandler.events = [];
  PubSubFlowObserver.events = [];
  const eventsEndpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  const publisher = await startApp(
    nestModule('PubSubPublisherModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .addFanoutChannel('ps.events')
            .enablePublisher(eventsEndpoint)
          .build())
      ]
    })
  );
  const subscribers = [];

  try {
    const alphaSubscriber = await startPubSubSubscriber('PubSubAlphaModule', PubSubAlphaHandler, eventsEndpoint);
    let betaSubscriber = await startPubSubSubscriber('PubSubBetaModule', PubSubBetaHandler, eventsEndpoint);
    const gammaSubscriber = await startPubSubSubscriber('PubSubGammaModule', PubSubGammaHandler, eventsEndpoint);
    subscribers.push(alphaSubscriber, betaSubscriber, gammaSubscriber);

    const fanout = publisher.app.get(nestjs.ZLINK_FANOUT_CLIENT, { strict: false });
    await publishUntil(fanout, 'all', -1, () =>
      PubSubAlphaHandler.events.some((event) => event.topic === 'all')
      && PubSubBetaHandler.events.some((event) => event.topic === 'all')
      && PubSubGammaHandler.events.some((event) => event.topic === 'all'));
    PubSubAlphaHandler.events = [];
    PubSubBetaHandler.events = [];
    PubSubGammaHandler.events = [];

    for (let seq = 1; seq <= 5; seq += 1) {
      await publishEvent(fanout, 'all', seq);
    }
    await waitFor(() => commonSequence([PubSubAlphaHandler.events, PubSubBetaHandler.events, PubSubGammaHandler.events], 'all', [1, 2, 3, 4, 5]));
    marker('PS-A1');

    PubSubAlphaHandler.events = [];
    PubSubBetaHandler.events = [];
    PubSubGammaHandler.events = [];
    await publishEvent(fanout, 'alpha', 10);
    await publishEvent(fanout, 'beta', 11);
    await publishEvent(fanout, 'gamma', 12);
    await waitFor(() =>
      hasOnlyTopics(PubSubAlphaHandler.events, ['alpha'])
      && hasOnlyTopics(PubSubBetaHandler.events, ['beta'])
      && hasOnlyTopics(PubSubGammaHandler.events, ['alpha', 'gamma'])
      && PubSubAlphaHandler.events.length === 1
      && PubSubBetaHandler.events.length === 1
      && PubSubGammaHandler.events.length === 2);
    marker('PS-A2');

    PubSubLateHandler.events = [];
    await publishEvent(fanout, 'all', 30);
    await waitFor(() => PubSubAlphaHandler.events.some((event) => event.seq === 30));
    subscribers.push(await startPubSubSubscriber('PubSubLateModule', PubSubLateHandler, eventsEndpoint));
    await publishUntil(fanout, 'all', 31, () =>
      PubSubLateHandler.events.some((event) => event.topic === 'all' && event.seq === 31));
    assert.equal(PubSubLateHandler.events.some((event) => event.seq === 30), false);
    marker('PS-A3');

    PubSubAlphaHandler.events = [];
    PubSubBetaHandler.events = [];
    PubSubGammaHandler.events = [];
    await betaSubscriber.app.close();
    subscribers.splice(subscribers.indexOf(betaSubscriber), 1);
    await publishUntil(fanout, 'all', 40, () =>
      PubSubAlphaHandler.events.some((event) => event.seq === 40)
      && PubSubGammaHandler.events.some((event) => event.seq === 40));
    assert.equal(PubSubBetaHandler.events.some((event) => event.seq === 40), false);
    betaSubscriber = await startPubSubSubscriber('PubSubBetaRestartedModule', PubSubBetaHandler, eventsEndpoint);
    subscribers.push(betaSubscriber);
    await publishUntil(fanout, 'all', 41, () =>
      PubSubAlphaHandler.events.some((event) => event.seq === 41)
      && PubSubBetaHandler.events.some((event) => event.seq === 41)
      && PubSubGammaHandler.events.some((event) => event.seq === 41));
    assert.equal(PubSubBetaHandler.events.some((event) => event.seq === 40), false);
    marker('PS-A4');

    PubSubAlphaHandler.events = [];
    PubSubBetaHandler.events = [];
    PubSubGammaHandler.events = [];
    PubSubSlowHandler.started = [];
    PubSubSlowHandler.events = [];
    subscribers.push(await startPubSubSubscriber('PubSubSlowModule', PubSubSlowHandler, eventsEndpoint));
    await publishEvent(fanout, 'all', 50);
    await waitFor(() => PubSubSlowHandler.started.some((event) => event.seq === 50));
    await publishEvent(fanout, 'all', 51);
    await waitFor(() =>
      PubSubAlphaHandler.events.some((event) => event.seq === 51)
      && PubSubBetaHandler.events.some((event) => event.seq === 51)
      && PubSubGammaHandler.events.some((event) => event.seq === 51)
      && !PubSubSlowHandler.events.some((event) => event.seq === 50),
      400);
    marker('PS-B1');

    PubSubAlphaHandler.events = [];
    PubSubBetaHandler.events = [];
    PubSubGammaHandler.events = [];
    PubSubFlowObserver.events = [];
    await fanout
      .publishToChannel('ps.events', 'all', { seq: 98 })
      .packetName('ps.missing')
      .submit();
    await waitFor(() => PubSubFlowObserver.events.some((event) =>
      event.packetName === 'ps.missing'
      && event.topic === 'all'
      && event.outcome === 'error'
      && event.errorReason === 'handlerMissing'
      && event.errorAction === 'drop'));
    await publishUntil(fanout, 'all', 99, () =>
      commonSequence([PubSubAlphaHandler.events, PubSubBetaHandler.events, PubSubGammaHandler.events], 'all', [99]));
    marker('PS-C1');
  } finally {
    for (const subscriber of subscribers.reverse()) {
      await subscriber.app.close();
    }
    await publisher.app.close();
  }
}

async function registrationCodec() {
  applyRcDecorators();
  RcDecoratedRequestHandler.requests = [];
  RcDecoratedSendHandler.messages = [];
  RcManualRequestHandler.requests = [];
  RcManualSendHandler.messages = [];
  const apiEndpoint = uniqueEndpoint('rc-api');
  const nestjs = loadNest();
  const { app } = await startApp(
    nestModule('RegistrationCodecModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .codecs().addJson()
          .addClientServerChannel('rc.api')
            .enableServer(apiEndpoint)
            .enableClient(apiEndpoint)
            .addHandlerGroup('rc-decorated')
            .addRequestHandler('rc.echo.manual', RcManualRequestHandler)
            .addSendHandler('rc.audit.manual', RcManualSendHandler)
          .build())
      ],
      providers: [
        RcDecoratedRequestHandler,
        RcDecoratedSendHandler,
        RcManualRequestHandler,
        RcManualSendHandler
      ]
    })
  );

  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const reply = await client
      .requestToChannel('rc.api', { id: 21, codec: 'json', ok: true })
      .packetName('rc.echo.manual')
      .submit();
    assert.equal(reply.id, 21);
    assert.equal(reply.codec, 'json');
    assert.equal(reply.ok, true);
    assert.equal(RcManualRequestHandler.requests.length, 1);
    await client
      .sendToChannel('rc.api', { id: 22, codec: 'json', ok: true })
      .packetName('rc.audit.manual')
      .submit();
    await waitFor(() => RcManualSendHandler.messages.some((message) => message.id === 22));
    marker('RC-A3');

    const decoratedReply = await client
      .requestToChannel('rc.api', { id: 23, codec: 'json', ok: true })
      .packetName('rc.echo.decorated')
      .submit();
    assert.equal(decoratedReply.id, 23);
    assert.equal(decoratedReply.codec, 'json');
    assert.equal(decoratedReply.ok, true);
    assert.equal(RcDecoratedRequestHandler.requests.length, 1);
    await client
      .sendToChannel('rc.api', { id: 24, codec: 'json', ok: true })
      .packetName('rc.audit.decorated')
      .submit();
    await waitFor(() => RcDecoratedSendHandler.messages.some((message) => message.id === 24));
    marker('RC-A2');
    await assertInvalidRegistration(nestjs);
    marker('RC-A6');
    selfCheck('RC-JSON-REQUEST');
  } finally {
    await app.close();
  }
}

async function monitoring() {
  const logFile = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'zlink-node-mon-')), 'flow.log');
  const apiEndpoint = uniqueEndpoint('mon-api');
  const nestjs = loadNest();
  const framework = loadFramework();
  const builder = nestjs.zlinkFramework();
  builder.configureDispatch()
    .messageFlow(framework.ZLinkMessageFlowLogMode.Verbose)
    .traceLogFile(logFile);
  builder.addClientServerChannel('mon.api')
    .enableServer(apiEndpoint)
    .enableClient(apiEndpoint)
    .addRequestHandler('mon.echo', EchoHandler);
  const { app } = await startApp(
    nestModule('MonitoringModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(builder.build())
      ],
      providers: [EchoHandler]
    })
  );

  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    await client.requestToChannel('mon.api', { ok: true }).packetName('mon.echo').submit();
    await waitFor(() => fs.existsSync(logFile) && fs.readFileSync(logFile, 'utf8').includes('mon.echo'));
    selfCheck('MON-DISPATCH-TRACE');
  } finally {
    await app.close();
  }
}

async function discoveryRegistryHa() {
  const nestjs = loadNest();
  const { app } = await startApp(
    nestModule('DiscoveryRegistryHaModule', {
      imports: [
        nestjs.ZLinkRegistryModule.forRoot({
          pubEndpoint: uniqueEndpoint('dr-pub'),
          routerEndpoint: uniqueEndpoint('dr-router'),
          registryId: 61
        })
      ]
    })
  );

  try {
    const query = app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    const status = await query.status();
    assert.equal(status.registryId, 61);
    selfCheck('DR-STANDALONE-STATUS');
  } finally {
    await app.close();
  }
}

async function resilienceLifecycle() {
  const endpointA = uniqueEndpoint('rl-api-a');
  const endpointB = uniqueEndpoint('rl-api-b');

  let first = await startApp(
    nestModule('ResilienceFirstModule', {
      imports: [
        loadNest().ZLinkModule.forRoot(loadNest().zlinkFramework()
          .addClientServerChannel('rl.api')
            .enableServer(endpointA)
            .enableClient(endpointA)
            .addRequestHandler('rl.echo', EchoHandler)
          .build())
      ],
      providers: [EchoHandler]
    })
  );
  const nestjs = loadNest();
  const firstClient = first.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
  assert.equal((await firstClient.requestToChannel('rl.api', { step: 1 }).packetName('rl.echo').submit()).step, 1);
  await first.app.close();

  first = await startApp(
    nestModule('ResilienceSecondModule', {
      imports: [
        loadNest().ZLinkModule.forRoot(loadNest().zlinkFramework()
          .addClientServerChannel('rl.api')
            .enableServer(endpointB)
            .enableClient(endpointB)
            .addRequestHandler('rl.echo', EchoHandler)
          .build())
      ],
      providers: [EchoHandler]
    })
  );

  try {
    const secondClient = first.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    assert.equal((await secondClient.requestToChannel('rl.api', { step: 2 }).packetName('rl.echo').submit()).step, 2);
    selfCheck('RL-RUNTIME-RESTART-SMOKE');
  } finally {
    await first.app.close();
  }
}

async function spotService() {
  const nestjs = loadNest();
  const { app } = await startApp(
    nestModule('SpotServiceModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .addSpotMesh('sm.play')
            .routingId('sm-node-a')
            .addEntrySpot(EntrySpot)
            .addSpotFactory(UserSpot)
          .build())
      ],
      providers: []
    })
  );

  try {
    const spotManager = app.get(nestjs.ZLINK_SPOT_MANAGER, { strict: false });
    const created = await spotManager.create(UserSpot, { owner: 'u1' });
    assert.equal(created.state, 'created');
    assert.equal(await spotManager.close(created.spotRid), true);
    selfCheck('SM-SPOT-MANAGER-CREATE-CLOSE');
  } finally {
    await app.close();
  }
}

class EchoHandler {
  handle(payload, context) {
    return { ...payload, handledBy: context.routingId ?? 'rm-provider-a' };
  }
}

class AuditHandler {
  static messages = [];
  handle(payload) {
    AuditHandler.messages.push(payload);
  }
}

class PubSubAlphaHandler {
  static events = [];
  handle(payload, context) {
    if (context.topic === 'all' || context.topic === 'alpha') {
      PubSubAlphaHandler.events.push({ topic: context.topic, seq: payload.seq });
    }
  }
}

class PubSubBetaHandler {
  static events = [];
  handle(payload, context) {
    if (context.topic === 'all' || context.topic === 'beta') {
      PubSubBetaHandler.events.push({ topic: context.topic, seq: payload.seq });
    }
  }
}

class PubSubGammaHandler {
  static events = [];
  handle(payload, context) {
    if (context.topic === 'all' || context.topic === 'alpha' || context.topic === 'gamma') {
      PubSubGammaHandler.events.push({ topic: context.topic, seq: payload.seq });
    }
  }
}

class PubSubLateHandler {
  static events = [];
  handle(payload, context) {
    if (context.topic === 'all') {
      PubSubLateHandler.events.push({ topic: context.topic, seq: payload.seq });
    }
  }
}

class PubSubSlowHandler {
  static started = [];
  static events = [];
  async handle(payload, context) {
    if (context.topic !== 'all') {
      return;
    }
    PubSubSlowHandler.started.push({ topic: context.topic, seq: payload.seq });
    await new Promise((resolve) => setTimeout(resolve, 1000));
    PubSubSlowHandler.events.push({ topic: context.topic, seq: payload.seq });
  }
}

class PubSubFlowObserver {
  static events = [];
  onMessageFlow(flow) {
    PubSubFlowObserver.events.push(flow);
  }
}

class RcDecoratedRequestHandler {
  static requests = [];
  handle(payload, context) {
    RcDecoratedRequestHandler.requests.push({ ...payload, contentType: context.contentType });
    return { ...payload, contentType: context.contentType };
  }
}

class RcDecoratedSendHandler {
  static messages = [];
  handle(payload, context) {
    RcDecoratedSendHandler.messages.push({ ...payload, contentType: context.contentType });
  }
}

class RcManualRequestHandler {
  static requests = [];
  handle(payload, context) {
    RcManualRequestHandler.requests.push({ ...payload, contentType: context.contentType });
    return { ...payload, contentType: context.contentType };
  }
}

class RcManualSendHandler {
  static messages = [];
  handle(payload, context) {
    RcManualSendHandler.messages.push({ ...payload, contentType: context.contentType });
  }
}

function applyRcDecorators() {
  if (applyRcDecorators.applied === true) {
    return;
  }
  const nestjs = loadNest();
  nestjs.zlinkRequestHandler('rc-decorated', 'rc.echo.decorated')(RcDecoratedRequestHandler);
  nestjs.zlinkSendHandler('rc-decorated', 'rc.audit.decorated')(RcDecoratedSendHandler);
  applyRcDecorators.applied = true;
}

async function assertInvalidRegistration(nestjs) {
  await assert.rejects(
    async () => {
      const { app } = await startApp(
        nestModule('RegistrationCodecInvalidDuplicateModule', {
          imports: [
            nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
              .addClientServerChannel('rc.invalid.duplicate')
                .enableServer(uniqueEndpoint('rc-invalid-duplicate'))
                .addRequestHandler('rc.duplicate', RcManualRequestHandler)
                .addRequestHandler('rc.duplicate', RcDecoratedRequestHandler)
              .build())
          ],
          providers: [RcManualRequestHandler, RcDecoratedRequestHandler]
        })
      );
      await app.close();
    },
    /duplicate|same|rc\.duplicate/i
  );
  assert.throws(
    () => nestjs.zlinkRequestHandler('', 'rc.invalid.group')(class InvalidGroupHandler {}),
    /handler group|empty/i
  );
  await assert.rejects(
    async () => {
      const { app } = await startApp(
        nestModule('RegistrationCodecInvalidChannelKindModule', {
          imports: [
            nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
              .addFanoutChannel('rc.invalid.kind')
                .enablePublisher(uniqueEndpoint('rc-invalid-kind'))
                .addPublishHandler('rc.invalid.publish', PubSubAlphaHandler)
              .build())
          ],
          providers: [PubSubAlphaHandler]
        })
      );
      await app.close();
    },
    /publish handlers require a subscriber|subscriber capability|rc\.invalid\.kind/i
  );
}

class EntrySpot {}

class UserSpot {
  async onCreate(request) {
    const payload = request.decode();
    this.owner = payload.owner;
    return { accepted: true, reply: { owner: this.owner } };
  }
}

async function startApp(rootModule) {
  const { NestFactory } = require('@nestjs/core');
  const app = await NestFactory.createApplicationContext(rootModule, {
    logger: false,
    abortOnError: false
  });
  return { app };
}

function loadNest() {
  return require(path.join(nodeRoot, 'packages/nestjs/dist'));
}

function loadFramework() {
  return require(path.join(nodeRoot, 'packages/framework/dist'));
}

function nestModule(name, metadata) {
  const { Module } = require('@nestjs/common');
  const moduleClass = { [name]: class {} }[name];
  Module(metadata)(moduleClass);
  return moduleClass;
}

function marker(id) {
  console.log(`scenario ${id} passed`);
}

function selfCheck(id) {
  console.log(`self-check ${id} passed`);
}

async function startPubSubSubscriber(moduleName, handlerType, endpoint) {
  const nestjs = loadNest();
  const framework = loadFramework();
  const builder = nestjs.zlinkFramework();
  builder.configureDispatch()
    .setMessageFlowObserver(PubSubFlowObserver)
    .messageFlow(framework.ZLinkMessageFlowLogMode.ErrorsOnly)
    .traceLogFile(path.join(os.tmpdir(), `zlink-node-ps-flow-${process.pid}.log`));
  builder.addFanoutChannel('ps.events')
    .enableSubscriber(endpoint)
    .addPublishHandler('ps.event', handlerType);
  return await startApp(
    nestModule(moduleName, {
      imports: [
        nestjs.ZLinkModule.forRoot(builder.build())
      ],
      providers: [handlerType, PubSubFlowObserver]
    })
  );
}

async function publishUntil(fanout, topic, seq, predicate) {
  for (let attempt = 0; attempt < 100; attempt += 1) {
    await publishEvent(fanout, topic, seq);
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 30));
  }
  throw new Error(`Timed out waiting for fanout subscribers on topic ${topic}.`);
}

async function publishEvent(fanout, topic, seq) {
  await fanout
    .publishToChannel('ps.events', topic, { seq })
    .packetName('ps.event')
    .submit();
}

function commonSequence(eventGroups, topic, sequence) {
  return eventGroups.every((events) => {
    const actual = events
      .filter((event) => event.topic === topic && event.seq > 0)
      .map((event) => event.seq);
    return containsSequence(actual, sequence);
  });
}

function containsSequence(actual, expected) {
  for (let start = 0; start <= actual.length - expected.length; start += 1) {
    if (expected.every((seq, index) => actual[start + index] === seq)) {
      return true;
    }
  }
  return false;
}

function hasOnlyTopics(events, topics) {
  const allowed = new Set(topics);
  return events.every((event) => allowed.has(event.topic));
}

function uniqueEndpoint(label) {
  return `inproc://node-e2e-${label}-${process.pid}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

async function reserveTcpEndpoint() {
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const address = server.address();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.close(resolve);
  });
  return `tcp://127.0.0.1:${address.port}`;
}

async function waitFor(predicate, timeoutMs = 3000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
  throw new Error('Timed out waiting for Node e2e condition.');
}

main().catch((error) => {
  console.error(error && error.stack ? error.stack : error);
  process.exit(1);
});
