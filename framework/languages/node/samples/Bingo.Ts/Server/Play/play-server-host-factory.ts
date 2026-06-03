const { startChannelServer } = require('../../../../shared/channel-runtime');
const { SampleBoundSessionRuntime } = require('../../../../shared/bound-session-runtime');
const { PlayerActorFactory } = require('./Actors/player-actor-factory');
const { BingoNotificationPublisher } = require('./BingoRoomSpots/bingo-notification-publisher');
const { BingoRoomJoinHandler } = require('./BingoRoomSpots/Handlers/bingo-room-join-handler');
const { StartBingoGameHandler } = require('./BingoRoomSpots/Handlers/start-bingo-game-handler');
const { BingoRoomTimerHandler } = require('./BingoRoomSpots/Handlers/bingo-room-timer-handler');
const { BingoEntrySpot } = require('./EntrySpot/bingo-entry-spot');
const { MatchBingoActorHandler } = require('./EntrySpot/Handlers/match-bingo-actor-handler');
const { AllocateBingoRoomHandler } = require('./Handlers/allocate-bingo-room-handler');
const { BingoRoomDirectory } = require('./Handlers/bingo-room-directory');
const { EnsurePlayerActorHandler } = require('./Handlers/ensure-player-actor-handler');
const { MatchBingoChannelHandler } = require('./Handlers/match-bingo-channel-handler');
const { StartBingoGameChannelHandler } = require('./Handlers/start-bingo-game-channel-handler');

async function buildPlayServerHost(options) {
  return await startChannelServer({
    endpoint: options.playEndpoint,
    channelName: 'bingo.play',
    handlerGroups: ['play'],
    providers: [
      SampleBoundSessionRuntime,
      {
        provide: PlayerActorFactory,
        inject: [SampleBoundSessionRuntime],
        useFactory: (boundSessions) => new PlayerActorFactory(boundSessions)
      },
      BingoNotificationPublisher,
      {
        provide: BingoRoomDirectory,
        inject: [BingoNotificationPublisher],
        useFactory: (notifications) => new BingoRoomDirectory(notifications)
      },
      BingoRoomJoinHandler,
      StartBingoGameHandler,
      BingoRoomTimerHandler,
      {
        provide: BingoEntrySpot,
        inject: [BingoRoomDirectory, BingoRoomJoinHandler],
        useFactory: (rooms, roomJoin) => new BingoEntrySpot(rooms, roomJoin)
      },
      MatchBingoActorHandler,
      {
        provide: AllocateBingoRoomHandler,
        inject: [BingoRoomDirectory],
        useFactory: (rooms) => new AllocateBingoRoomHandler(rooms)
      },
      {
        provide: EnsurePlayerActorHandler,
        inject: [PlayerActorFactory],
        useFactory: (actorFactory) => new EnsurePlayerActorHandler(actorFactory)
      },
      {
        provide: MatchBingoChannelHandler,
        inject: [PlayerActorFactory, BingoEntrySpot, MatchBingoActorHandler],
        useFactory: (actorFactory, entrySpot, matchBingoActor) =>
          new MatchBingoChannelHandler(actorFactory, entrySpot, matchBingoActor)
      },
      {
        provide: StartBingoGameChannelHandler,
        inject: [PlayerActorFactory, BingoRoomDirectory, StartBingoGameHandler, BingoRoomTimerHandler],
        useFactory: (actorFactory, rooms, startBingoGame, timer) =>
          new StartBingoGameChannelHandler(actorFactory, rooms, startBingoGame, timer)
      }
    ],
    handlers: (providers) => [
      {
        group: 'play',
        packetName: 'BingoNotificationsReq',
        handle: (request) => providers.get(SampleBoundSessionRuntime).deliveredFor(request.actorId, request.afterSeq)
      }
    ]
  });
}

export { buildPlayServerHost };
