require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkFramework, zlinkHandlers } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { createRegistryClient } = require('../discovery-support');
const { BingoNotificationDeliveryLog } = require('./notification-delivery-log');
const { PlayerActorFactory } = require('./Adapters/ZLink/Actors/player-actor-factory');
const { BingoNotificationPublisher } = require('./Adapters/ZLink/Notifications/bingo-notification-publisher');
const { BingoRoomTimerHandler } = require('./Adapters/ZLink/Spots/Handlers/bingo-room-timer-handler');
const { BingoEntrySpot } = require('./Adapters/ZLink/Spots/bingo-entry-spot');
const { BingoRoomSpot } = require('./Adapters/ZLink/Spots/bingo-room-spot');
const { MatchBingoActorHandler } = require('./Adapters/ZLink/Spots/Handlers/match-bingo-actor-handler');
const { AllocateBingoRoomHandler } = require('./Adapters/ZLink/Handlers/allocate-bingo-room-handler');
const { BingoRoomAllocator } = require('./Application/RoomAllocation/bingo-room-allocator');
const { BingoNotificationsHandler } = require('./Adapters/ZLink/Handlers/bingo-notifications-handler');
const { EnsurePlayerActorHandler } = require('./Adapters/ZLink/Handlers/ensure-player-actor-handler');
const { MatchBingoChannelHandler } = require('./Adapters/ZLink/Handlers/match-bingo-channel-handler');
const { SubmitBingoCardHandler } = require('./Adapters/ZLink/Spots/Handlers/submit-bingo-card-handler');
const { SubmitBingoCardChannelHandler } = require('./Adapters/ZLink/Handlers/submit-bingo-card-channel-handler');
const { SampleNames } = require('../Configuration/sample-names');
const { loadSampleConfig } = require('../Configuration/sample-config');
const { PacketNames } = require('../../Shared/Contracts/messages');

async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  class BingoPlayModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .clientServerChannel(SampleNames.playChannel, (channel) => channel
            .server(config.playEndpoint)
            .handlerGroup('play'))
          .clientServerChannel(SampleNames.notificationChannel, (channel) => channel
            .server(config.notificationEndpoint)
            .handlerGroup('notifications'))
          .actorFactory(SampleNames.playerActorType, PlayerActorFactory)
          .spotNode(SampleNames.roomSpotType, (spot) => spot
            .entrySpot(BingoEntrySpot)
            .spotFactory(BingoRoomSpot))
          .build()
      })
    ],
    providers: [
      BingoNotificationDeliveryLog,
      PlayerActorFactory,
      BingoNotificationPublisher,
      BingoRoomAllocator,
      BingoRoomTimerHandler,
      SubmitBingoCardHandler,
      BingoEntrySpot,
      MatchBingoActorHandler,
      ...zlinkHandlers('play')
        .request(AllocateBingoRoomHandler, PacketNames.allocateBingoRoom)
        .request(EnsurePlayerActorHandler, PacketNames.ensurePlayerActorReq)
        .request(MatchBingoChannelHandler, PacketNames.matchBingoReq)
        .request(SubmitBingoCardChannelHandler, PacketNames.submitBingoCardReq)
        .providers(),
      ...zlinkHandlers('notifications')
        .request(BingoNotificationsHandler, PacketNames.bingoNotificationsReq)
        .providers()
    ]
  })(BingoPlayModule);

  const app = await NestFactory.createApplicationContext(BingoPlayModule, {
    logger: false,
    abortOnError: false
  });
  const registry = await createRegistryClient(config.registryEndpoint);
  await registry.register(SampleNames.playService, config.playEndpoint);
  await registry.register(SampleNames.notificationService, config.notificationEndpoint);

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.playEndpoint,
    channelName: SampleNames.playChannel
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await registry.stop();
    await closeNestRuntime(app);
  }
}

bootstrap().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
