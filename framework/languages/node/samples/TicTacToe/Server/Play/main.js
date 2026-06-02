const { startChannelServer } = require('../../../shared/channel-runtime');
const { CreateGameHandler } = require('./Handlers/create-game-handler');

async function main() {
  const createGameHandler = new CreateGameHandler();
  await startChannelServer({
    endpoint: process.env.TICTACTOE_PLAY_ENDPOINT,
    channelName: 'play',
    handlers: [
      { packetName: 'CreateGame', handle: (request) => createGameHandler.handle(request) },
      { packetName: 'Ping', handle: () => ({ ok: true }) }
    ]
  });
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
