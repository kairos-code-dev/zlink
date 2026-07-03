const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const { spawn } = require('node:child_process');

const zlink = require('../../../../bindings/node/dist');
const framework = require('../packages/framework/dist/internal');
const backend = require('../packages/framework/dist/runtime/backend');
const connector = require('../packages/stream-connector/dist');
const { ZLinkRedisLocationStore } = require('../packages/framework-locations-redis/dist');

const repoRoot = path.resolve(__dirname, '../../../..');
const dotnetTestHostProject = path.join(
  repoRoot,
  'framework/languages/dotnet/testapps/Zlink.Framework.TestHost/Zlink.Framework.TestHost.csproj'
);
const dotnetRedisTestsProject = path.join(
  repoRoot,
  'framework/languages/dotnet/tests/Zlink.Framework.Locations.Redis.Tests/Zlink.Framework.Locations.Redis.Tests.csproj'
);

async function main() {
  const results = [];
  await runInTempDir(async (tempDir) => {
    results.push(...await nodeClientToDotnetChannelServer(tempDir));
    results.push(await nodePublisherToDotnetFanoutSubscriber(tempDir));
    results.push(await dotnetClientToNodeChannelServer(tempDir));
    results.push(await nodeConnectorToDotnetStreamServer(tempDir));
    results.push(await dotnetConnectorToNodeStreamServer(tempDir));
    results.push(...await nodeDotnetRedisLocationRows(tempDir));
  });

  for (const result of results) {
    console.log(`ok - ${result}`);
  }
}

async function nodeDotnetRedisLocationRows(tempDir) {
  const redis = await startRedisContainer();
  const prefix = `zlink:cross:node:${process.pid}:${Date.now()}`;
  try {
    await writeNodeLocationRows(redis.endpoint, `${prefix}:node`);
    await runDotnetRedisCrossLanguageTest(
      'FullyQualifiedName~RedisCrossLanguageTests.Dotnet_Reads_Node_Rows',
      redis.endpoint,
      prefix,
      tempDir
    );
    await runDotnetRedisCrossLanguageTest(
      'FullyQualifiedName~RedisCrossLanguageTests.Dotnet_Writes_Rows_For_Node_To_Read',
      redis.endpoint,
      prefix,
      tempDir
    );
    await assertNodeReadsDotnetLocationRows(redis.endpoint, `${prefix}:dotnet`);
    return [
      'Node Redis location rows -> dotnet location store',
      'dotnet Redis location rows -> Node location store'
    ];
  } finally {
    await redis.stop();
  }
}

async function writeNodeLocationRows(redisEndpoint, keyPrefix) {
  const store = new ZLinkRedisLocationStore({
    url: `redis://${redisEndpoint}`,
    keyPrefix
  });
  try {
    await store.renewOwnerLease('node-owner', rid('node-node'), 30000);
    assert.equal((await store.updatePeer({
      autoConnectType: framework.ZLinkLocationAutoConnectType.RouteMesh,
      meshName: 'cross',
      nodeRid: rid('node-node'),
      role: framework.ZLinkLocationRole.Router,
      endpoint: 'tcp://127.0.0.1:5320',
      weight: 100,
      value: 11n,
      metadata: { 'route-endpoint': 'tcp://127.0.0.1:6320' },
      capabilities: ['node', 'route'],
      ownerId: 'node-owner',
      generation: 0n,
      updatedAt: new Date(0)
    }, framework.ZLinkLocationWriteIntent.NewClaim)).status, framework.ZLinkLocationWriteStatus.Stored);
    assert.equal((await store.updateSpot({
      meshName: 'cross',
      spotRid: rid('node-spot'),
      spotType: 'node-game',
      nodeRid: rid('node-node'),
      spotKind: framework.ZLinkSpotKind.User,
      routeEndpoint: 'tcp://127.0.0.1:5320',
      ownerId: 'node-owner',
      generation: 0n,
      updatedAt: new Date(0)
    }, framework.ZLinkLocationWriteIntent.NewClaim)).status, framework.ZLinkLocationWriteStatus.Stored);
    assert.equal((await store.updateActor({
      actorType: 'player',
      actorId: 'node-actor',
      actorRef: 'node-ref',
      nodeRid: rid('node-node'),
      generation: 0n,
      locationKind: framework.ZLinkSpotKind.User,
      spotMeshName: 'cross',
      spotRid: rid('node-spot'),
      spotKind: framework.ZLinkSpotKind.User,
      ownerId: 'node-owner',
      updatedAt: new Date(0)
    }, framework.ZLinkLocationWriteIntent.NewClaim)).status, framework.ZLinkLocationWriteStatus.Stored);
    assert.equal((await store.updateRoute({
      routeKind: framework.ZLinkRouteKind.ActorSession,
      routeKey: 'node-route',
      ownerNodeRid: rid('node-node'),
      ownerId: 'node-owner',
      generation: 0n,
      value: Uint8Array.from([5, 6, 7, 8]),
      updatedAt: new Date(0)
    }, framework.ZLinkLocationWriteIntent.NewClaim)).status, framework.ZLinkLocationWriteStatus.Stored);
  } finally {
    await store.dispose();
  }
}

async function assertNodeReadsDotnetLocationRows(redisEndpoint, keyPrefix) {
  const store = new ZLinkRedisLocationStore({
    url: `redis://${redisEndpoint}`,
    keyPrefix
  });
  try {
    const actor = await store.resolveActor({ actorType: 'player', actorId: 'dotnet-actor' });
    assert.equal(actor.actorRef, 'dotnet-ref');
    assert.equal(actor.nodeRid.toHex(), rid('dotnet-node').toHex());
    assert.equal(actor.ownerId, 'dotnet-owner');

    const spot = await store.resolveSpot({ meshName: 'cross', spotRid: rid('dotnet-spot') });
    assert.equal(spot.spotType, 'dotnet-game');
    assert.equal(spot.nodeRid.toHex(), rid('dotnet-node').toHex());

    const route = await store.resolveRoute({
      routeKind: framework.ZLinkRouteKind.ActorSession,
      routeKey: 'dotnet-route'
    });
    assert.deepEqual([...route.value], [9, 8, 7, 6]);

    const peers = await store.listPeers({
      autoConnectType: framework.ZLinkLocationAutoConnectType.RouteMesh,
      meshName: 'cross',
      role: framework.ZLinkLocationRole.Router,
      nodeRid: rid('dotnet-node')
    });
    const peer = peers.find((row) => row.endpoint === 'tcp://127.0.0.1:5310');
    assert.ok(peer);
    assert.equal(peer.metadata['route-endpoint'], 'tcp://127.0.0.1:6310');
    assert.deepEqual(peer.capabilities, ['dotnet', 'route']);
  } finally {
    await store.dispose();
  }
}

async function nodeClientToDotnetChannelServer(tempDir) {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const eventFile = path.join(tempDir, 'node-client-dotnet-channel.events');
  const host = startDotnetHost(tempDir, 'node-client-dotnet-channel', [
    'channel-server',
    '--channel-name', 'profiles',
    '--server-endpoint', endpoint,
    '--event-file', eventFile
  ]);
  const ctx = zlink.createContext();
  const dealer = zlink.createDealerSocket(ctx);
  let monitor;

  try {
    await host.ready;
    monitor = dealer.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    dealer.connect(endpoint);
    await waitForMonitorEvent(monitor, zlink.MonitorEventType.ConnectionReady, 7000, 'Node dealer -> dotnet channel server');
    monitor.close();
    monitor = undefined;
    const registration = framework.createFrameworkRegistration({
      channels: {
        profiles: { client: { manualConnections: [endpoint] } }
      }
    });
    const client = new framework.DefaultZLinkChannelClient(
      registration,
      new framework.ZLinkDealerChannelClientTransport(dealer)
    );
    const reply = await withTimeout(
      client
        .requestToChannel('profiles', { value: 'node-to-dotnet' })
        .packetName('TestHostProfileRequest')
        .timeout(5000)
        .submit(),
      7000,
      'Node client -> dotnet channel server'
    );

    assert.deepEqual(reply, { value: 'node-to-dotnet' });
    await client
      .sendToChannel('profiles', { value: 'node-send-to-dotnet' })
      .packetName('TestHostProfileSend')
      .submit();
    await waitForFileText(eventFile, (text) => text.includes('channel-server-send|node-send-to-dotnet'), 7000);
    return [
      'Node client -> dotnet channel server request/reply',
      'Node client -> dotnet channel server one-way send'
    ];
  } finally {
    try {
      monitor?.close();
    } catch {}
    dealer.close();
    ctx.close();
    await host.stop();
  }
}

async function nodePublisherToDotnetFanoutSubscriber(tempDir) {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const eventFile = path.join(tempDir, 'node-publisher-dotnet-subscriber.events');
  const topic = 'profile.changed';
  const ctx = zlink.createContext();
  const publisher = zlink.createPubSocket(ctx);
  const dealer = zlink.createDealerSocket(ctx);
  const monitor = publisher.monitorOpen([zlink.MonitorEventType.ConnectionReady]);

  try {
    publisher.bind(endpoint);
    const host = startDotnetHost(tempDir, 'node-publisher-dotnet-subscriber', [
      'channel-subscriber',
      '--channel-name', 'profiles',
      '--publisher-endpoint', endpoint,
      '--event-file', eventFile
    ]);
    try {
      await host.ready;
      await waitForMonitorEvent(monitor, zlink.MonitorEventType.ConnectionReady, 7000, 'Node publisher -> dotnet fanout subscriber');
      monitor.close();

      const registration = framework.createFrameworkRegistration({
        channels: {
          profiles: { publisher: { bind: endpoint } }
        }
      });
      const fanout = new framework.DefaultZLinkFanoutClient(
        registration,
        new framework.ZLinkDealerChannelClientTransport(dealer, publisher)
      );

      await fanout
        .publishToChannel('profiles', topic, { value: 'node-publish-to-dotnet' })
        .packetName('TestHostPublishedEvent')
        .submit();
      await waitForFileText(
        eventFile,
        (text) => text.includes(`${topic}:node-publish-to-dotnet`),
        7000
      );
    } finally {
      await host.stop();
    }
    return 'Node publisher -> dotnet fanout subscriber';
  } finally {
    try {
      monitor.close();
    } catch {}
    dealer.close();
    publisher.close();
    ctx.close();
  }
}

async function dotnetClientToNodeChannelServer(tempDir) {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const eventFile = path.join(tempDir, 'dotnet-client-node-channel.events');
  const ctx = zlink.createContext();
  const router = zlink.createRouterSocket(ctx);
  let closed = false;

  const dispatcher = new framework.ZLinkChannelRequestDispatcher({
    channelName: 'profiles',
    dispatchErrors: noDispatchErrorReporter(),
    handlers: new Map([
      ['TestHostProfileRequest', {
        async handle(payload) {
          const value = payload?.Value ?? payload?.value;
          return { Value: `${value}|node` };
        }
      }]
    ])
  });

  async function pump() {
    while (!closed) {
      const received = new zlink.Received();
      try {
        if (router.recv(received, zlink.RecvFlags.DontWait)) {
          await dispatcher.dispatch(received, router);
          received.close();
          return;
        }
        received.close();
      } catch (error) {
        received.close();
        if (!closed) {
          throw error;
        }
      }
      await new Promise((resolve) => setImmediate(resolve));
    }
  }

  try {
    router.bind(endpoint);
    const pumpResult = pump();
    const host = startDotnetHost(tempDir, 'dotnet-client-node-channel', [
      'channel-client',
      '--channel-name', 'profiles',
      '--server-endpoint', endpoint,
      '--event-file', eventFile,
      '--publish-value', 'dotnet-to-node'
    ]);
    try {
      await host.ready;
      await waitForFileText(eventFile, (text) => text.includes('channel-client|dotnet-to-node|node'), 7000);
      await withTimeout(pumpResult, 1000, 'Node channel server dispatch');
    } finally {
      await host.stop();
    }
    return 'dotnet client -> Node channel server';
  } finally {
    closed = true;
    router.close();
    ctx.close();
  }
}

async function nodeConnectorToDotnetStreamServer(tempDir) {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const eventFile = path.join(tempDir, 'node-connector-dotnet-stream.events');
  const host = startDotnetHost(tempDir, 'node-connector-dotnet-stream', [
    'stream-raw',
    '--stream-endpoint', endpoint,
    '--event-file', eventFile
  ]);
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint,
    heartbeat: { enabled: false },
    reconnect: { enabled: false },
    requestTimeoutMs: 5000
  });

  try {
    await host.ready;
    await instance.connect();
    const pending = instance
      .request({
        codec: connector.ZlinkStreamCodec.Json,
        payload: new TextEncoder().encode('"ping"')
      })
      .packetName('RawPing')
      .compress()
      .timeout(5000)
      .submit();

    await instance.dispatch();
    const reply = await withTimeout(pending, 7000, 'Node stream connector -> dotnet stream server');
    assert.equal(new TextDecoder().decode(reply.payload), '"pong"');

    await waitForFileText(eventFile, (text) => text.includes('raw|ping'), 5000);
    return 'Node stream connector -> dotnet stream server';
  } finally {
    await instance.close();
    await host.stop();
  }
}

async function dotnetConnectorToNodeStreamServer(tempDir) {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const eventFile = path.join(tempDir, 'dotnet-connector-node-stream.events');
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const context = factory.createChannelAdapter().createContext();
  const socket = factory.createStreamAdapter().createStreamSocket(context);
  const bindingRuntime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: {
      createTextMessage(payload) {
        return zlink.Message.from(Buffer.from(payload));
      },
      createBinaryMessage(payload) {
        return zlink.Message.from(Buffer.from(payload));
      }
    }
  });
  const streamEvents = [];
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    headerDecoder: (header) => {
      streamEvents.push(`header:${header.data().length}`);
      return connector.ZlinkStreamHeaderCodec.decode(header.data());
    },
    bindingRuntime,
    onError(error) {
      streamEvents.push(`error:${error instanceof Error ? error.message : String(error)}`);
    },
    sessionFactory(sessionContext) {
      return {
        context: sessionContext,
        async onDispatch(_header, payload) {
          const value = payload.decode();
          streamEvents.push(`dispatch:${value}`);
          assert.equal(value, 'dotnet-to-node');
          sessionContext.client.reply('node-pong').compress().submit();
          streamEvents.push('reply:sent');
        }
      };
    }
  });

  try {
    runtime.start();
    socket.bind(endpoint);
    const host = startDotnetHost(tempDir, 'dotnet-connector-node-stream', [
      'stream-client',
      '--stream-endpoint', endpoint,
      '--event-file', eventFile,
      '--publish-value', 'dotnet-to-node'
    ]);
    try {
      await host.ready;
      await waitForFileText(eventFile, (text) => text.includes('stream-client|') && text.includes('node-pong'), 7000);
    } catch (error) {
      if (streamEvents.length > 0) {
        error.message += `\nNode stream events: ${streamEvents.join(', ')}`;
      }
      throw error;
    } finally {
      try {
        await host.stop();
      } catch (error) {
        if (streamEvents.length > 0) {
          error.message += `\nNode stream events: ${streamEvents.join(', ')}`;
        }
        throw error;
      }
    }
    return 'dotnet connector -> Node stream server';
  } finally {
    await runtime.dispose();
    socket.close();
    context.close();
  }
}

function startDotnetHost(tempDir, name, args) {
  const readyFile = path.join(tempDir, `${name}.ready.json`);
  const stopFile = path.join(tempDir, `${name}.stop`);
  const child = spawn('dotnet', [
    'run',
    '--project', dotnetTestHostProject,
    '--framework', 'net8.0',
    '--',
    '--ready-file', readyFile,
    '--stop-file', stopFile,
    ...args
  ], {
    cwd: repoRoot,
    stdio: ['ignore', 'pipe', 'pipe']
  });
  const output = [];
  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');
  child.stdout.on('data', (chunk) => output.push(chunk));
  child.stderr.on('data', (chunk) => output.push(chunk));

  const exit = new Promise((resolve) => {
    child.on('exit', (code, signal) => resolve({ code, signal }));
  });

  return {
    ready: waitForReadyFile(readyFile, exit, output, 30000),
    async stop() {
      if (child.exitCode !== null) {
        return;
      }
      await fs.writeFile(stopFile, 'STOP');
      const result = await withTimeout(exit, 10000, `stop ${name}`);
      if (result.code !== 0) {
        throw new Error(`${name} exited with ${result.code ?? result.signal}\n${output.join('')}`);
      }
    }
  };
}

function noDispatchErrorReporter() {
  return new framework.ZLinkDispatchErrorReporter(
    undefined,
    undefined,
    { reportRuntimeTaskException() {} }
  );
}

async function waitForReadyFile(readyFile, exit, output, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      await fs.access(readyFile);
      return;
    } catch {
      const exited = await pollExit(exit);
      if (exited !== undefined) {
        throw new Error(`dotnet test host exited before ready: ${exited.code ?? exited.signal}\n${output.join('')}`);
      }
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
  }
  throw new Error(`dotnet test host did not become ready\n${output.join('')}`);
}

async function waitForFileText(filePath, predicate, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let lastText = '';
  while (Date.now() < deadline) {
    try {
      const text = await fs.readFile(filePath, 'utf8');
      lastText = text;
      if (predicate(text)) {
        return text;
      }
    } catch {}
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  throw new Error(`expected event text did not appear in ${filePath}\nLast text:\n${lastText}`);
}

async function waitForMonitorEvent(monitor, eventType, timeoutMs, label) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const event = monitor.recv(zlink.RecvFlags.DontWait);
    if (event !== null && event.event === eventType) {
      return event;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  throw new Error(`${label} monitor event timed out`);
}

async function pollExit(exit) {
  const marker = Symbol('running');
  const result = await Promise.race([
    exit,
    Promise.resolve(marker)
  ]);
  return result === marker ? undefined : result;
}

async function reservePort() {
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function runInTempDir(callback) {
  const tempDir = await fs.mkdtemp(path.join(os.tmpdir(), 'zlink-node-cross-'));
  try {
    await callback(tempDir);
  } finally {
    await fs.rm(tempDir, { recursive: true, force: true });
  }
}

async function startRedisContainer() {
  const name = `zlink-node-dotnet-location-${process.pid}-${Date.now()}`;
  const containerId = (await runProcess('docker', [
    'run', '-d', '--rm',
    '--name', name,
    '-p', '127.0.0.1::6379',
    'redis:7.2-alpine'
  ], { cwd: repoRoot })).trim();
  const portLine = (await runProcess('docker', ['port', containerId, '6379/tcp'], { cwd: repoRoot })).trim();
  const port = portLine.split(':').at(-1);
  const endpoint = `127.0.0.1:${port}`;
  await waitTcp(endpoint, 10000);
  return {
    endpoint,
    async stop() {
      await runProcess('docker', ['rm', '-f', containerId], { cwd: repoRoot, allowFailure: true });
    }
  };
}

async function runDotnetRedisCrossLanguageTest(filter, redisEndpoint, prefix, tempDir) {
  await runProcess('dotnet', [
    'test',
    dotnetRedisTestsProject,
    '--framework', 'net8.0',
    '--filter', filter,
    '--logger', `trx;LogFileName=${path.basename(filter).replace(/[^A-Za-z0-9_.-]/g, '_')}.trx`,
    '--results-directory', tempDir
  ], {
    cwd: repoRoot,
    env: {
      ...process.env,
      ZLINK_REDIS_TEST_ENDPOINT: redisEndpoint,
      ZLINK_REDIS_CROSS_LANGUAGE_PREFIX: prefix
    }
  });
}

async function runProcess(command, args, options = {}) {
  const child = spawn(command, args, {
    cwd: options.cwd ?? repoRoot,
    env: options.env ?? process.env,
    stdio: ['ignore', 'pipe', 'pipe']
  });
  const chunks = [];
  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');
  child.stdout.on('data', (chunk) => chunks.push(chunk));
  child.stderr.on('data', (chunk) => chunks.push(chunk));
  const result = await new Promise((resolve) => {
    child.on('exit', (code, signal) => resolve({ code, signal }));
  });
  const output = chunks.join('');
  if (result.code !== 0 && options.allowFailure !== true) {
    throw new Error(`${command} ${args.join(' ')} failed with ${result.code ?? result.signal}\n${output}`);
  }
  return output;
}

async function waitTcp(endpoint, timeoutMs) {
  const [host, portText] = endpoint.split(':');
  const port = Number(portText);
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await canConnectTcp(host, port)) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  throw new Error(`Redis did not accept TCP connections at ${endpoint}`);
}

function canConnectTcp(host, port) {
  return new Promise((resolve) => {
    const socket = net.createConnection({ host, port });
    socket.once('connect', () => {
      socket.destroy();
      resolve(true);
    });
    socket.once('error', () => {
      socket.destroy();
      resolve(false);
    });
  });
}

function rid(value) {
  return zlink.RoutingId.from(value);
}

function withTimeout(promise, timeoutMs, label) {
  let timeout;
  const guard = new Promise((_, reject) => {
    timeout = setTimeout(() => reject(new Error(`${label} timed out after ${timeoutMs}ms`)), timeoutMs);
  });
  return Promise.race([promise, guard]).finally(() => clearTimeout(timeout));
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
