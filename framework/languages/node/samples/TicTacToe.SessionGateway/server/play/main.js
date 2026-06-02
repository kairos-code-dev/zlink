const { startRouteServer } = require('../../../shared/route-runtime');

startRouteServer({
  endpoint: process.env.TICTACTOE_SG_PLAY_ENDPOINT,
  routingId: 'play-server',
  handlers: [{ packetName: 'Ping', handle: () => ({ role: 'play-server' }) }]
}).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
