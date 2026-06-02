const { createChannelClient, startChannelServer } = require('../../../shared/channel-runtime');
const { CreateGameHttpHandler } = require('./handlers/create-game-http-handler');

async function main() {
  const playClient = await createChannelClient({
    channelName: 'play',
    peers: [process.env.TICTACTOE_PLAY_ENDPOINT]
  });
  const createGame = new CreateGameHttpHandler(async () => playClient);
  await startChannelServer({
    endpoint: process.env.TICTACTOE_API_ENDPOINT,
    channelName: 'api',
    beforeReady: () => playClient.request('Ping', {}, 1000),
    handlers: [{
      packetName: 'RunTicTacToe',
      handle: (request) => createGame.handle(request)
    }]
  });
  await playClient.stop();
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
