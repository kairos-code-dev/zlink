const { buildSessionServerHost } = require('./session-server-host-factory');

buildSessionServerHost({
  sessionEndpoint: process.env.BINGO_SESSION_ENDPOINT
}).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
