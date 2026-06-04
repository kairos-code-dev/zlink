const framework = require('../../../../packages/framework/dist');
const { startRouteServer } = require('../../../shared/route-runtime');
const { SampleBoundSessionRuntime } = require('../../../shared/bound-session-runtime');
const { SessionPlayerActorFactory } = require('../../Shared/Actors/player-actor');
const { SampleNames } = require('../../Shared/Configuration/sample-names');
const { PacketNames } = require('../../Shared/Contracts/messages');
const { JoinMatchHandler } = require('./EntrySpot/Handlers/join-match-handler');
const { TicTacToeEntrySpot } = require('./EntrySpot/tictactoe-entry-spot');
const { PlaceMarkHandler } = require('./GameSpots/Handlers/place-mark-handler');
const { TicTacToeMatchDirectory } = require('./GameSpots/tictactoe-match-directory');
const { CreateMatchRoomHandler } = require('./Handlers/create-match-room-handler');
const { EnsurePlayerActorHandler } = require('./Handlers/ensure-player-actor-handler');

async function main() {
  const boundSessions = new SampleBoundSessionRuntime();
  const actorManager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([[SampleNames.playerActorType, SessionPlayerActorFactory]]),
    boundSessionFactory(actorId) {
      return boundSessions.createBoundSession(actorId);
    }
  });
  const matches = new TicTacToeMatchDirectory();
  const ensurePlayer = new EnsurePlayerActorHandler(actorManager, boundSessions);
  const createMatch = new CreateMatchRoomHandler(matches);
  const joinMatch = new JoinMatchHandler(actorManager, matches);
  const entrySpot = new TicTacToeEntrySpot(joinMatch);
  const placeMark = new PlaceMarkHandler(matches);

  await startRouteServer({
    endpoint: process.env.TICTACTOE_SG_PLAY_ENDPOINT,
    routingId: SampleNames.playNode,
    handlers: [
      { packetName: PacketNames.ensurePlayerActorReq, handle: (request) => ensurePlayer.handle(request) },
      { packetName: PacketNames.createMatchReq, handle: (request) => createMatch.handle(request) },
      { packetName: PacketNames.joinMatchReq, handle: (request) => entrySpot.handle(request) },
      { packetName: PacketNames.placeMarkReq, handle: (request) => placeMark.handle(request) },
      {
        packetName: PacketNames.notificationsReq,
        handle: (request) => boundSessions.deliveredFor(request.actorId, request.afterSeq)
      },
      { packetName: 'Ping', handle: () => ({ role: SampleNames.playNode }) }
    ]
  });
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
