const { startRouteServer } = require('../../../shared/route-runtime');

startRouteServer({
  endpoint: process.env.TICTACTOE_SG_API_ENDPOINT,
  routingId: 'api-server',
  handlers: [{ packetName: 'Ping', handle: () => ({ role: 'api-server' }) }]
}).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
