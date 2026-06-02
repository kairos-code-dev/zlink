const { startChannelServer } = require('../../../shared/channel-runtime');
const { SampleBoundSessionRuntime } = require('../../../shared/bound-session-runtime');
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
const { RunBingoRoomTimerHandler } = require('./Handlers/run-bingo-room-timer-handler');

async function buildPlayServerHost(options) {
  const boundSessions = new SampleBoundSessionRuntime();
  const actorFactory = new PlayerActorFactory(boundSessions);
  const notifications = new BingoNotificationPublisher();
  const rooms = new BingoRoomDirectory(notifications);
  const roomJoin = new BingoRoomJoinHandler();
  const startBingoGame = new StartBingoGameHandler();
  const timer = new BingoRoomTimerHandler();
  const entrySpot = new BingoEntrySpot(rooms, roomJoin);
  const matchBingoActor = new MatchBingoActorHandler();
  const allocateBingoRoom = new AllocateBingoRoomHandler(rooms);
  const ensurePlayerActor = new EnsurePlayerActorHandler(actorFactory);
  const matchBingo = new MatchBingoChannelHandler(actorFactory, entrySpot, matchBingoActor);
  const startBingo = new StartBingoGameChannelHandler(actorFactory, rooms, startBingoGame);
  const runTimer = new RunBingoRoomTimerHandler(rooms, timer);

  return await startChannelServer({
    endpoint: options.playEndpoint,
    channelName: 'bingo.play',
    handlerGroups: ['play'],
    handlers: [
      { group: 'play', packetName: 'AllocateBingoRoom', handle: (request) => allocateBingoRoom.handle(request) },
      { group: 'play', packetName: 'EnsurePlayerActorReq', handle: (request) => ensurePlayerActor.handle(request) },
      { group: 'play', packetName: 'MatchBingoReq', handle: (request) => matchBingo.handle(request) },
      { group: 'play', packetName: 'StartBingoGameReq', handle: (request) => startBingo.handle(request) },
      { group: 'play', packetName: 'RunBingoRoomTimerReq', handle: (request) => runTimer.handle(request) },
      { group: 'play', packetName: 'BingoDeliveredNotificationsReq', handle: () => ({ delivered: boundSessions.delivered }) },
      { group: 'play', packetName: 'Ping', handle: () => ({ ok: true }) }
    ]
  });
}

module.exports = { buildPlayServerHost };
