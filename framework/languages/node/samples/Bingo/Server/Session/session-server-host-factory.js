const { startRouteServer } = require('../../../shared/route-runtime');

async function buildSessionServerHost(options) {
  return await startRouteServer({
    endpoint: options.sessionEndpoint,
    routingId: 'session-server',
    handlers: [{ packetName: 'Ping', handle: () => ({ role: 'session-server' }) }]
  });
}

module.exports = { buildSessionServerHost };
