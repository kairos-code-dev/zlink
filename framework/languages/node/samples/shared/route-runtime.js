const { createZLinkNestRuntime, nestjs } = require('./nestjs-provider-runtime');
const {
  closeNestRuntime,
  decodePayload,
  retry,
  waitForShutdown
} = require('./runtime-common');

function createRouteOptions({ endpoint, routingId, peers = [], handlers = [] }) {
  return {
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
  };
}

async function startRouteServer({ endpoint, routingId, peers = [], handlers, providers = [] }) {
  let container;
  const resolver = {
    get(token) {
      if (container === undefined) {
        throw new Error(`NestJS provider is not ready: ${String(token)}`);
      }
      return container.get(token);
    }
  };
  const resolvedHandlers = typeof handlers === 'function' ? handlers(resolver) : handlers;
  container = await createZLinkNestRuntime(
    createRouteOptions({ endpoint, routingId, peers, handlers: resolvedHandlers }),
    providers
  );
  process.stdout.write(`${JSON.stringify({ event: 'ready', endpoint, routingId })}\n`);
  await waitForShutdown();
  await closeNestRuntime(container);
}

async function createRouteClient({ endpoint, routingId, peers }) {
  const container = await createZLinkNestRuntime(createRouteOptions({ endpoint, routingId, peers }));
  const client = container.get(nestjs.ZLINK_ROUTE_CLIENT);
  return {
    async request(targetNodeRid, packetName, payload, timeoutMs = 1000) {
      return await retry(() => client
        .request('sample-route', targetNodeRid, payload)
        .packetName(packetName)
        .timeout(timeoutMs)
        .submit());
    },
    async stop() {
      await closeNestRuntime(container);
    }
  };
}

module.exports = { createRouteClient, startRouteServer };
