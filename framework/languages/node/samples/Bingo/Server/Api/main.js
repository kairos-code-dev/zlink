const { createRouteClient, startRouteServer } = require('../../../shared/route-runtime');

async function main() {
  let playClient;
  await startRouteServer({
    endpoint: process.env.BINGO_API_ENDPOINT,
    routingId: 'api-server',
    peers: [process.env.BINGO_PLAY_ENDPOINT],
    handlers: [{
      packetName: 'RunBingo',
      async handle() {
        playClient ??= await createRouteClient({
          endpoint: process.env.BINGO_API_CLIENT_ENDPOINT,
          routingId: 'api-client',
          peers: [process.env.BINGO_PLAY_ENDPOINT]
        });
        return await playClient.request('play-server', 'RunBingoRoom', {
          players: [
            { actorId: 'p1', numbers: [7] },
            { actorId: 'p2', numbers: [9] },
            { actorId: 'p3', numbers: [7] },
            { actorId: 'p4', numbers: [11] }
          ],
          draws: [7, 9, 11]
        });
      }
    }]
  });
  await playClient?.stop();
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
