const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const framework = require('@zlink-systems/framework');
const nestjs = require('@zlink-systems/nestjs');
const {
  ZLinkRedisLocationStore
} = require('@zlink-systems/framework-locations-redis');

const role = requireText(process.env.ZLINK_TEST_ROLE, 'ZLINK_TEST_ROLE');
const meshName = requireText(process.env.ZLINK_TEST_MESH, 'ZLINK_TEST_MESH');
const endpoint = requireText(process.env.ZLINK_TEST_ENDPOINT, 'ZLINK_TEST_ENDPOINT');
const redisUrl = requireText(process.env.ZLINK_TEST_REDIS_URL, 'ZLINK_TEST_REDIS_URL');
const keyPrefix = requireText(process.env.ZLINK_TEST_REDIS_PREFIX, 'ZLINK_TEST_REDIS_PREFIX');
const routingId = requireText(process.env.ZLINK_TEST_ROUTING_ID, 'ZLINK_TEST_ROUTING_ID');
const targetRoutingId = requireText(
  process.env.ZLINK_TEST_TARGET_ROUTING_ID,
  'ZLINK_TEST_TARGET_ROUTING_ID'
);

class RemoteRoomSpot {
  constructor(context) {
    this.context = context;
  }

  configure() {}

  async onInitialize() {}

  async onClosing() {}
}

let app;
let store;
let spotManager;
let currentSpot;
let stopping = false;

void start().catch((error) => {
  send({
    type: 'fatal',
    message: error?.stack ?? String(error)
  });
  process.exitCode = 1;
});

async function start() {
  store = new ZLinkRedisLocationStore({
    url: redisUrl,
    keyPrefix
  });
  const builder = nestjs.zlinkFramework().addLocationStore(store);
  builder.configureLocations()
    .pollingIntervalMs(20)
    .heartbeatIntervalMs(100)
    .ownerLeaseTtlMs(5_000);
  const mesh = builder.addRouteMesh(meshName)
    .listen(endpoint)
    .routingId(routingId);
  if (role === 'target') {
    mesh.objects().server().addSpotFactory(
      'RemoteRoom',
      RemoteRoomSpot,
      (factory) => factory.disableRelocation()
    );
  } else if (role === 'source') {
    mesh.objects().client();
  } else {
    throw new Error(`Unsupported ZLINK_TEST_ROLE '${role}'.`);
  }

  class FixtureModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(builder.build())]
  })(FixtureModule);
  app = await NestFactory.createApplicationContext(FixtureModule, {
    logger: false,
    abortOnError: false
  });
  spotManager = app.get(nestjs.ZLINK_SPOT_MANAGER, { strict: false });
  if (spotManager === undefined || spotManager === null) {
    throw new Error('ZLINK_SPOT_MANAGER was not registered.');
  }
  await waitForPublishedTarget();
  process.on('message', onMessage);
  process.once('SIGTERM', () => void stop(0));
  process.once('SIGINT', () => void stop(0));
  send({ type: 'ready', role, pid: process.pid });
}

async function waitForPublishedTarget() {
  const deadline = Date.now() + 5_000;
  let observed = [];
  do {
    const descriptors = await store.listMeshNodes(meshName);
    observed = descriptors;
    if (descriptors.some((descriptor) =>
      String(descriptor.rid) === targetRoutingId
      && (
        role !== 'target'
        || (
          descriptor.state === 1
          && descriptor.objectRole === 'server'
          && descriptor.objectCapabilities.some((capability) =>
            capability.objectKind === 'user_spot'
            && capability.stableType === 'RemoteRoom'
          )
        )
      )
    )) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 20));
  } while (Date.now() < deadline);
  throw new Error(
    (role === 'target'
      ? 'Target MeshNode descriptor was not published.'
      : 'No MeshNode descriptor was visible to the source.')
      + ` Observed=${JSON.stringify(observed, (_, value) =>
        typeof value === 'bigint' ? String(value) : value)}`
  );
}

async function onMessage(message) {
  if (message?.type === 'stop') {
    await stop(0);
    return;
  }
  if (role !== 'source' || typeof message?.id !== 'number') return;
  try {
    const value = await execute(message.action);
    send({ type: 'result', id: message.id, value });
  } catch (error) {
    send({
      type: 'error',
      id: message.id,
      message: JSON.stringify({
        stack: error?.stack ?? String(error),
        kind: error?.kind,
        retryable: error?.retryable,
        cause: error?.cause?.stack ?? error?.cause
      })
    });
  }
}

async function execute(action) {
  switch (action) {
    case 'create': {
      const result = await spotManager
        .create('RemoteRoom')
        .inMesh(meshName)
        .request({ sourcePid: process.pid })
        .timeout(5_000)
        .submit();
      currentSpot = result.spot;
      return normalizeResult(result);
    }
    case 'getOrCreate': {
      const result = await spotManager
        .getOrCreate(currentSpot.spotId, 'RemoteRoom')
        .inMesh(meshName)
        .timeout(5_000)
        .submit();
      return normalizeResult(result);
    }
    case 'find':
      return normalizeSpot(await spotManager.find(currentSpot.spotId));
    case 'close':
      return await spotManager.close(currentSpot);
    case 'findAfterClose':
      return normalizeSpot(await spotManager.find(currentSpot.spotId));
    default:
      throw new Error(`Unsupported action '${action}'.`);
  }
}

function normalizeResult(result) {
  return {
    state: result.state,
    spot: normalizeSpot(result.spot)
  };
}

function normalizeSpot(spot) {
  if (spot === undefined) return null;
  return {
    spotId: String(spot.spotId),
    objectGeneration: String(spot.objectGeneration),
    meshName: spot.meshName,
    nodeRid: String(spot.nodeRid)
  };
}

async function stop(exitCode) {
  if (stopping) return;
  stopping = true;
  try {
    await app?.close();
  } finally {
    await store?.dispose();
    process.exit(exitCode);
  }
}

function send(message) {
  if (typeof process.send === 'function' && process.connected) {
    process.send(message);
  }
}

function requireText(value, name) {
  if (typeof value !== 'string' || value.length === 0) {
    throw new Error(`${name} is required.`);
  }
  return value;
}
