import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { zlinkProtobufCodec } from '@zlink-systems/framework-codec-protobuf';
import { PlayerActorFactory } from './Adapters/ZLink/Actors/player-actor-factory';
import { BingoEntrySpot } from './Adapters/ZLink/Spots/bingo-entry-spot';
import { BingoRoomSpot } from './Adapters/ZLink/Spots/bingo-room-spot';
import { AllocateBingoRoomHandler } from './Adapters/ZLink/Handlers/allocate-bingo-room-handler';
import { EnsurePlayerActorHandler } from './Adapters/ZLink/Handlers/ensure-player-actor-handler';
import { MatchBingoActorHandler } from './Adapters/ZLink/Spots/Handlers/match-bingo-actor-handler';
import { ObserveBingoEventsHandler } from './Adapters/ZLink/Spots/Handlers/observe-bingo-events-handler';
import { StopObservingBingoEventsHandler } from './Adapters/ZLink/Spots/Handlers/stop-observing-bingo-events-handler';
import { SubmitBingoCardHandler } from './Adapters/ZLink/Spots/Handlers/submit-bingo-card-handler';
import { BingoRoomTimerHandler } from './Adapters/ZLink/Spots/Handlers/bingo-room-timer-handler';
import { BingoRewardAcquiredEventHandler } from './Adapters/ZLink/Spots/Handlers/bingo-reward-acquired-event-handler';
import { RedisBingoMatchQueue } from './Adapters/ZLink/Matchmaking/redis-bingo-match-queue';
import { BingoRoomAllocator } from './Application/RoomAllocation/bingo-room-allocator';
import { BINGO_MATCH_QUEUE } from './Application/RoomAllocation/bingo-match-queue';
import { SampleNames, SampleTimings } from '../Configuration/sample-names';
import { BINGO_SAMPLE_CONFIG } from '../Configuration/sample-config';
import { PacketNames } from '../../Shared/Contracts/messages';
function createBingoPlayModule(config: {
  registryRouterEndpoint: string;
  playEndpoint: string;
  playRouteEndpoint: string;
  playSpotEndpoint: string;
  playSpotPubSubEndpoint: string;
  playSpotNodeRid: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
}) {
  class BingoPlayModule {}

  zlinkModule(__dirname, {
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .options({
            requestTimeoutMs: SampleTimings.requestTimeout,
            registrySpotRemoteAddresses: {
              namespace: SampleNames.roomSpotNode,
              routerChannelId: SampleNames.roomRouteChannel
            }
          })
          .codecs()
            .use(zlinkProtobufCodec())
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.playChannel)
            .enableServer(config.playEndpoint)
            .addHandlerGroup('play')
          .addRouteMeshChannel(SampleNames.roomRouteChannel)
            .enableRouter(config.playRouteEndpoint)
            .routingId(config.playSpotNodeRid)
            .addRequestHandler(PacketNames.ensurePlayerActorReq, EnsurePlayerActorHandler)
          .actorFactory(SampleNames.playerActorType, PlayerActorFactory)
          .addSpotNode(SampleNames.roomSpotNode)
            .enableRouter(config.playSpotEndpoint, config.playSpotNodeRid)
            .enablePubSub(config.playSpotPubSubEndpoint, config.playSpotNodeRid)
            .attachSpotPublisherClient(SampleNames.roomRewardChannel)
            .acceptSpotRoutesFromChannel(SampleNames.roomRouteChannel)
            .addEntrySpot(BingoEntrySpot)
            .addSpotFactory(BingoRoomSpot)
          .build()
      })
    ],
    providers: [
      { provide: BINGO_SAMPLE_CONFIG, useValue: config },
      {
        provide: BINGO_MATCH_QUEUE,
        useFactory: () => new RedisBingoMatchQueue(config.redisEndpoint, config.redisKeyPrefix)
      },
      AllocateBingoRoomHandler,
      EnsurePlayerActorHandler,
      PlayerActorFactory,
      BingoRoomAllocator,
      BingoEntrySpot,
      MatchBingoActorHandler,
      ObserveBingoEventsHandler,
      StopObservingBingoEventsHandler,
      SubmitBingoCardHandler,
      BingoRoomTimerHandler,
      BingoRewardAcquiredEventHandler
    ]
  })(BingoPlayModule);

  return BingoPlayModule;
}

export { createBingoPlayModule };
