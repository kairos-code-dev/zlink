import { Module } from '@nestjs/common';
import * as path from 'node:path';
import { ZLinkModule, zlinkDiscoverProviders, zlinkFramework } from '@zlink-systems/nestjs';
import { BingoNotificationDeliveryLog } from './notification-delivery-log';
import { PlayerActorFactory } from './Adapters/ZLink/Actors/player-actor-factory';
import { BingoNotificationPublisher } from './Adapters/ZLink/Notifications/bingo-notification-publisher';
import { BingoEntrySpot } from './Adapters/ZLink/Spots/bingo-entry-spot';
import { BingoRoomSpot } from './Adapters/ZLink/Spots/bingo-room-spot';
import { BingoRoomAllocator } from './Application/RoomAllocation/bingo-room-allocator';
import { SampleNames, SampleTimings } from '../Configuration/sample-names';
function createBingoPlayModule(config: {
  registryRouterEndpoint: string;
  notificationEndpoint: string;
  playEndpoint: string;
}) {
  class BingoPlayModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .options({ requestTimeoutMs: SampleTimings.requestTimeout })
          .codecs()
            .addProtobuf()
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.playChannel)
            .enableServer(config.playEndpoint)
            .addHandlerGroup('play')
          .addClientServerChannel(SampleNames.notificationChannel)
            .enableServer(config.notificationEndpoint)
            .addHandlerGroup('notifications')
          .actorFactory(SampleNames.playerActorType, PlayerActorFactory)
          .addSpotNode(SampleNames.roomSpotType)
            .addEntrySpot(BingoEntrySpot)
            .addSpotFactory(BingoRoomSpot)
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
