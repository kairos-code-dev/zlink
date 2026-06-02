const { startRouteServer } = require('../../../shared/route-runtime');

startRouteServer({
  endpoint: process.env.TICTACTOE_SG_REGISTRY_ENDPOINT,
  routingId: 'registry-server',
  handlers: [{ packetName: 'Ping', handle: () => ({ role: 'registry-server' }) }]
}).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
