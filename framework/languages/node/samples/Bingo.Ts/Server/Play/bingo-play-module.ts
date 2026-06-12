const { Module } = require('@nestjs/common');
const path = require('node:path');
const { ZLinkModule, zlinkDiscoverProviders, zlinkFramework } = require('../../../../../packages/nestjs/dist');
const { BingoNotificationDeliveryLog } = require('./notification-delivery-log');
const { PlayerActorFactory } = require('./Adapters/ZLink/Actors/player-actor-factory');
const { BingoNotificationPublisher } = require('./Adapters/ZLink/Notifications/bingo-notification-publisher');
const { BingoEntrySpot } = require('./Adapters/ZLink/Spots/bingo-entry-spot');
const { BingoRoomSpot } = require('./Adapters/ZLink/Spots/bingo-room-spot');
const { BingoRoomAllocator } = require('./Application/RoomAllocation/bingo-room-allocator');
const { SampleNames } = require('../Configuration/sample-names');

function createBingoPlayModule(config: {
  notificationEndpoint: string;
  playEndpoint: string;
}) {
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
      BingoEntrySpot,
      ...zlinkDiscoverProviders(path.join(__dirname, 'Adapters', 'ZLink', 'Handlers')),
      ...zlinkDiscoverProviders(path.join(__dirname, 'Adapters', 'ZLink', 'Spots', 'Handlers'))
    ]
  })(BingoPlayModule);

  return BingoPlayModule;
}

export { createBingoPlayModule };
