require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkFramework, zlinkHandlers } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { SampleBoundSessionRuntime } = require('./bound-session-runtime');
const { PlayerActorFactory } = require('./Adapters/ZLink/Actors/player-actor-factory');
const { BingoNotificationPublisher } = require('./Adapters/ZLink/Notifications/bingo-notification-publisher');
const { BingoRoomTimerHandler } = require('./Adapters/ZLink/Spots/Handlers/bingo-room-timer-handler');
const { BingoEntrySpot } = require('./Adapters/ZLink/Spots/bingo-entry-spot');
const { MatchBingoActorHandler } = require('./Adapters/ZLink/Spots/Handlers/match-bingo-actor-handler');
const { AllocateBingoRoomHandler } = require('./Adapters/ZLink/Handlers/allocate-bingo-room-handler');
const { BingoRoomDirectory } = require('./Application/RoomAllocation/bingo-room-directory');
const { BingoNotificationsHandler } = require('./Adapters/ZLink/Handlers/bingo-notifications-handler');
const { EnsurePlayerActorHandler } = require('./Adapters/ZLink/Handlers/ensure-player-actor-handler');
const { MatchBingoChannelHandler } = require('./Adapters/ZLink/Handlers/match-bingo-channel-handler');
const { SubmitBingoCardHandler } = require('./Adapters/ZLink/Spots/Handlers/submit-bingo-card-handler');
const { SubmitBingoCardChannelHandler } = require('./Adapters/ZLink/Handlers/submit-bingo-card-channel-handler');
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
    BingoRoomTimerHandler,
    SubmitBingoCardHandler,
    BingoEntrySpot,
    MatchBingoActorHandler,
    ...zlinkHandlers('play')
      .request(BingoNotificationsHandler, PacketNames.bingoNotificationsReq)
      .request(AllocateBingoRoomHandler, PacketNames.allocateBingoRoom)
      .request(EnsurePlayerActorHandler, PacketNames.ensurePlayerActorReq)
      .request(MatchBingoChannelHandler, PacketNames.matchBingoReq)
      .request(SubmitBingoCardChannelHandler, PacketNames.submitBingoCardReq)
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
