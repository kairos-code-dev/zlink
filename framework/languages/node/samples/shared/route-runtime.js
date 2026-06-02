const framework = require('../../packages/framework/dist');

function createRouteRegistration({ endpoint, routingId, peers = [], handlers = [] }) {
  return framework.createFrameworkRegistration({
    routeChannels: [{
      routerChannelId: 'sample-route',
      bind: endpoint,
      routingId,
      manualConnections: peers,
      requestHandlers: handlers.map(({ packetName, handle }) => ({
        packetName,
        handler: {
          async handle(payload, context) {
            return await handle(decodePayload(payload), context);
          }
        }
      }))
    }]
  });
}

async function startRouteServer({ endpoint, routingId, peers = [], handlers }) {
  const registration = createRouteRegistration({ endpoint, routingId, peers, handlers });
  const runtime = new framework.ZLinkFrameworkRuntimeHost({ registration });
  await runtime.start();
  process.stdout.write(`${JSON.stringify({ event: 'ready', endpoint, routingId })}\n`);
  await waitForShutdown();
  await stopRuntime(runtime);
}

async function createRouteClient({ endpoint, routingId, peers }) {
  const registration = createRouteRegistration({ endpoint, routingId, peers });
  const runtime = new framework.ZLinkFrameworkRuntimeHost({ registration });
  await runtime.start();
  const client = new framework.DefaultZLinkRouteClient(registration, runtime.routeTransport);
  return {
    async request(targetNodeRid, packetName, payload, timeoutMs = 1000) {
      return await retry(() => client
        .request('sample-route', targetNodeRid, payload)
        .packetName(packetName)
        .timeout(timeoutMs)
        .submit());
    },
    async stop() {
      await stopRuntime(runtime);
    }
  };
}

async function retry(action) {
  let lastError;
  for (let attempt = 0; attempt < 5; attempt += 1) {
    try {
      return await action();
    } catch (error) {
      lastError = error;
      await new Promise((resolve) => setImmediate(resolve));
    }
  }
  throw lastError;
}

function waitForShutdown() {
  return new Promise((resolve) => {
    process.once('SIGINT', resolve);
    process.once('SIGTERM', resolve);
  });
}

module.exports = { createRouteClient, startRouteServer };

function decodePayload(payload) {
  if (Buffer.isBuffer(payload) || payload instanceof Uint8Array) {
    return JSON.parse(Buffer.from(payload).toString());
  }
  if (typeof payload === 'string') {
    return JSON.parse(payload);
  }
  return payload;
}

async function stopRuntime(runtime) {
  try {
    await runtime.stop();
  } catch (error) {
    if (error?.name === 'CloseError' && (error?.code === 0 || error?.code === 401)) {
      return;
    }
    throw error;
  }
}
