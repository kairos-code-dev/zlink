import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { zlinkProtobufCodec } from '@zlink-systems/framework-codec-protobuf';
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

  zlinkModule(__dirname, {
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .options({ requestTimeoutMs: SampleTimings.requestTimeout })
          .codecs()
            .use(zlinkProtobufCodec())
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
      BingoEntrySpot
    ]
  })(BingoPlayModule);

  return BingoPlayModule;
}

export { createBingoPlayModule };
