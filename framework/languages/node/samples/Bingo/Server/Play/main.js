require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkHandlerGroup } = require('../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../../../shared/runtime-common');
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
const { BingoNotificationsHandler } = require('./Handlers/bingo-notifications-handler');
const { EnsurePlayerActorHandler } = require('./Handlers/ensure-player-actor-handler');
const { MatchBingoChannelHandler } = require('./Handlers/match-bingo-channel-handler');
const { StartBingoGameChannelHandler } = require('./Handlers/start-bingo-game-channel-handler');

class BingoPlayModule {}

Module({
  imports: [
    ZLinkModule.forRoot({
      clientServerChannels: {
        'bingo.play': {
          server: { bind: process.env.BINGO_PLAY_ENDPOINT },
          handlerGroups: ['play']
        }
      }
    })
  ],
  providers: [
    SampleBoundSessionRuntime,
    PlayerActorFactory,
    BingoNotificationPublisher,
    BingoRoomDirectory,
    BingoRoomJoinHandler,
    StartBingoGameHandler,
    BingoRoomTimerHandler,
    BingoEntrySpot,
    MatchBingoActorHandler,
    ...zlinkHandlerGroup('play', [
      [BingoNotificationsHandler, 'BingoNotificationsReq'],
      [AllocateBingoRoomHandler, 'AllocateBingoRoom'],
      [EnsurePlayerActorHandler, 'EnsurePlayerActorReq'],
      [MatchBingoChannelHandler, 'MatchBingoReq'],
      [StartBingoGameChannelHandler, 'StartBingoGameReq']
    ])
  ]
})(BingoPlayModule);

async function bootstrap() {
  const app = await NestFactory.createApplicationContext(BingoPlayModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: process.env.BINGO_PLAY_ENDPOINT,
    channelName: 'bingo.play'
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await closeNestRuntime(app);
  }
}

bootstrap().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
