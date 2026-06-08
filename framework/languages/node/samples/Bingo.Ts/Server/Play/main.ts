require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkFramework, zlinkHandlers } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { SampleBoundSessionRuntime } = require('./bound-session-runtime');
const { PlayerActorFactory } = require('./Actors/player-actor-factory');
const { BingoNotificationPublisher } = require('./BingoRoomSpots/bingo-notification-publisher');
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
const { PacketNames } = require('../../Shared/Contracts/messages');

class BingoPlayModule {}

Module({
  imports: [
    ZLinkModule.forRoot(
      zlinkFramework()
        .clientServerChannel('bingo.play', (channel) => channel
          .server(process.env.BINGO_PLAY_ENDPOINT)
          .handlerGroup('play'))
        .build()
    )
  ],
  providers: [
    SampleBoundSessionRuntime,
    PlayerActorFactory,
    BingoNotificationPublisher,
    BingoRoomDirectory,
    StartBingoGameHandler,
    BingoRoomTimerHandler,
    BingoEntrySpot,
    MatchBingoActorHandler,
    ...zlinkHandlers('play')
      .request(BingoNotificationsHandler, PacketNames.bingoNotificationsReq)
      .request(AllocateBingoRoomHandler, PacketNames.allocateBingoRoom)
      .request(EnsurePlayerActorHandler, PacketNames.ensurePlayerActorReq)
      .request(MatchBingoChannelHandler, PacketNames.matchBingoReq)
      .request(StartBingoGameChannelHandler, PacketNames.startBingoGameReq)
      .providers()
  ]
})(BingoPlayModule);

async function bootstrap(): Promise<void> {
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

bootstrap().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
