const { startRouteServer } = require('../../../shared/route-runtime');

async function buildRegistryHost(options) {
  return await startRouteServer({
    endpoint: options.registryEndpoint,
    routingId: 'registry-server',
    handlers: [{ packetName: 'Ping', handle: () => ({ role: 'registry-server' }) }]
  });
}

module.exports = { buildRegistryHost };
