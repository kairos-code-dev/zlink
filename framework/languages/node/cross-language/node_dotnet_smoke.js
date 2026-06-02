const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const { spawn } = require('node:child_process');

const zlink = require('../../../../bindings/node/dist');
const framework = require('../packages/framework/dist');
const connector = require('../packages/stream-connector/dist');

const repoRoot = path.resolve(__dirname, '../../../..');
const dotnetTestHostProject = path.join(
  repoRoot,
  'framework/languages/dotnet/testapps/Zlink.Framework.TestHost/Zlink.Framework.TestHost.csproj'
);

async function main() {
  const results = [];
  await runInTempDir(async (tempDir) => {
    results.push(...await nodeClientToDotnetChannelServer(tempDir));
    results.push(await nodePublisherToDotnetFanoutSubscriber(tempDir));
    results.push(await dotnetClientToNodeChannelServer(tempDir));
    results.push(await nodeConnectorToDotnetStreamServer(tempDir));
    results.push(await dotnetConnectorToNodeStreamServer(tempDir));
  });

  for (const result of results) {
    console.log(`ok - ${result}`);
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

  try {
    await host.ready;
    const monitor = dealer.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    dealer.connect(endpoint);
    await waitForMonitorEvent(monitor, zlink.MonitorEventType.ConnectionReady, 7000, 'Node dealer -> dotnet channel server');
    monitor.close();

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
    handlers: new Map([
      ['TestHostProfileRequest', {
        async handle(payload) {
          const request = JSON.parse(payload.toString());
          return { value: `${request.value}|node` };
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
      .timeout(5000)
      .submit();

    await instance.dispatch();
    const reply = await withTimeout(pending, 7000, 'Node stream connector -> dotnet stream server');
    assert.equal(new TextDecoder().decode(reply.payload), '"pong"');

    await waitForFileText(eventFile, (text) => text.includes('raw|"ping"'), 5000);
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
  const factory = new framework.ZLinkNodeBackendAdapterFactory();
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
          streamEvents.push(`dispatch:${payload.getString()}`);
          assert.equal(payload.getString(), '"dotnet-to-node"');
          await sessionContext.client.reply('node-pong').submit();
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
      await waitForFileText(eventFile, (text) => text.includes('stream-client|"node-pong"'), 7000);
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
  while (Date.now() < deadline) {
    try {
      const text = await fs.readFile(filePath, 'utf8');
      if (predicate(text)) {
        return text;
      }
    } catch {}
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  throw new Error(`expected event text did not appear in ${filePath}`);
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
