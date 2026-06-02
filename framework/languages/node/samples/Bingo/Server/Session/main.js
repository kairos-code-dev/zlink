const { startRouteServer } = require('../../../shared/route-runtime');

startRouteServer({
  endpoint: process.env.BINGO_SESSION_ENDPOINT,
  routingId: 'session-server',
  handlers: [{ packetName: 'Ping', handle: () => ({ role: 'session-server' }) }]
}).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
