const { createRouteClient, startRouteServer } = require('../../../shared/route-runtime');
const { SampleNames, SampleTimings } = require('../../Shared/Configuration/sample-names');
const { PacketNames } = require('../../Shared/Contracts/messages');
const { AuthenticateActorHandler } = require('./Handlers/authenticate-actor-handler');
const { CreateMatchHandler } = require('./Handlers/create-match-handler');

async function main() {
  const playClient = await createRouteClient({
    endpoint: process.env.TICTACTOE_SG_API_CLIENT_ENDPOINT,
    routingId: 'api-client',
    peers: [process.env.TICTACTOE_SG_PLAY_ENDPOINT]
  });
  const authenticateActor = new AuthenticateActorHandler();
  const createMatch = new CreateMatchHandler(playClient);
  await startRouteServer({
    endpoint: process.env.TICTACTOE_SG_API_ENDPOINT,
    routingId: SampleNames.apiNode,
    handlers: [
      {
        packetName: PacketNames.authenticateActorReq,
        handle: (request) => authenticateActor.handle(request)
      },
      {
        packetName: PacketNames.createMatchReq,
        handle: (request) => createMatch.handle(request)
      },
      { packetName: 'Ping', handle: () => ({ role: SampleNames.apiNode, timeout: SampleTimings.requestTimeout }) }
    ]
  });
  await playClient.stop();
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
