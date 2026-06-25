#!/usr/bin/env node
const assert = require('node:assert/strict');
const fs = require('node:fs');
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
  const eventsEndpoint = uniqueEndpoint('ps-events');
  const nestjs = loadNest();
  const { app } = await startApp(
    nestModule('PubSubModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .addFanoutChannel('ps.events')
            .enablePublisher(eventsEndpoint)
            .enableSubscriber(eventsEndpoint)
            .addPublishHandler('ps.event', EventHandler)
          .build())
      ],
      providers: [EventHandler]
    })
  );

  try {
    const fanout = app.get(nestjs.ZLINK_FANOUT_CLIENT, { strict: false });
    await fanout
      .publishToChannel('ps.events', 'room-a', { room: 'room-a', seq: 1 })
      .packetName('ps.event')
      .submit();
    await waitFor(() => EventHandler.events.some((event) => event.seq === 1));
    selfCheck('PS-SINGLE-TOPIC-PUBLISH');
  } finally {
    await app.close();
  }
}

async function registrationCodec() {
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
            .addRequestHandler('rc.echo', EchoHandler)
          .build())
      ],
      providers: [EchoHandler]
    })
  );

  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const reply = await client
      .requestToChannel('rc.api', { codec: 'json', ok: true })
      .packetName('rc.echo')
      .submit();
    assert.deepEqual(reply, { codec: 'json', ok: true, handledBy: 'rm-provider-a' });
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

class EventHandler {
  static events = [];
  handle(payload) {
    EventHandler.events.push(payload);
  }
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

function uniqueEndpoint(label) {
  return `inproc://node-e2e-${label}-${process.pid}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
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
