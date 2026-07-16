import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode, ZLinkSocketEventKind } from '@zlink-systems/framework';
import { bingoFrameworkProtobuf } from '../../Shared/Contracts/protobuf-framework-codec';
import { PlayerActorFactory } from './Infrastructure/ZLink/Actors/player-actor-factory';
import { PlayerActor } from './Infrastructure/ZLink/Actors/player-actor';
import { PlayerActorTransferAdapter } from './Infrastructure/ZLink/Actors/player-actor-transfer-adapter';
import { BingoEntrySpot } from './Infrastructure/ZLink/Spots/EntrySpot/bingo-entry-spot';
import { BingoRoomSpot } from './Infrastructure/ZLink/Spots/BingoRoomSpot/bingo-room-spot';
import {
  AllocateBingoRoomHandler,
  AllocateBingoRoomSpotHandler,
  BingoRoomProvisioner
} from './Infrastructure/ZLink/Handlers/allocate-bingo-room-handler';
import { EnsurePlayerActorHandler } from './Infrastructure/ZLink/Handlers/ensure-player-actor-handler';
import { MatchBingoActorHandler } from './Infrastructure/ZLink/Spots/EntrySpot/Handlers/match-bingo-actor-handler';
import { ObserveBingoEventsHandler } from './Infrastructure/ZLink/Spots/EntrySpot/Handlers/observe-bingo-events-handler';
import { StopObservingBingoEventsHandler } from './Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/stop-observing-bingo-events-handler';
import { SubmitBingoCardHandler } from './Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/submit-bingo-card-handler';
import { BingoRoomTimerHandler } from './Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/bingo-room-timer-handler';
import { BingoRewardAcquiredEventHandler } from './Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/bingo-reward-acquired-event-handler';
import { RedisBingoMatchQueue } from './Infrastructure/ZLink/Matchmaking/redis-bingo-match-queue';
import { BingoRoomAllocator } from './Application/RoomAllocation/bingo-room-allocator';
import { BINGO_MATCH_QUEUE } from './Application/RoomAllocation/bingo-match-queue';
import { SampleNames } from '../Configuration/sample-names';
import { BINGO_SAMPLE_CONFIG, createBingoConfigurationModule } from '../Configuration/sample-config';
import type { BingoSampleConfig } from '../Configuration/sample-config';
import { bingoLocationOptions, createBingoLocationStore } from '../Configuration/location-store';
import { bingoMeterProvider } from '../runtime-support';
import {
  PlayRouterReadinessHandler
} from './Infrastructure/ZLink/Handlers/room-router-readiness-handler';
import { RoomRouterReadinessHandler } from '../Configuration/room-router-readiness-handler';
function createBingoPlayModule() {
  class BingoPlayModule {}
  const configuration = createBingoConfigurationModule([
    'playEndpoint',
    'playSpotEndpoint',
    'playSpotPubSubEndpoint',
    'redisEndpoint',
    'redisKeyPrefix',
    'logDir'
  ]);

  zlinkModule(__dirname, {
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [BINGO_SAMPLE_CONFIG],
        useFactory: (config: BingoSampleConfig) => {
          const builder = zlinkFramework();
          builder.options({
            metrics: { meterProvider: bingoMeterProvider },
            monitoring: {
              socket: [{
                sourceName: `${SampleNames.playChannel}.server`,
                events: [ZLinkSocketEventKind.ConnectionReady]
              }],
              spot: [{ sourceName: SampleNames.roomSpotNode, intervalMs: 100 }]
            }
          });
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${config.logDir}/flow-play.log`)
            .traceLabel('play');
          builder.addLocationStore(createBingoLocationStore(config));
          Object.assign(builder.configureLocations(), bingoLocationOptions());
          return builder
          .codecs()
            .use(bingoFrameworkProtobuf)
          .addClientServerChannel(SampleNames.playChannel)
            .useAllocatedRoutingId(2, 'play')
            .setRoutingIdAllocationGroup('bingo.play')
            .enableServer(config.playEndpoint)
            .enableClient()
            .addHandlerGroup('play')
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient()
          .addActorTransferAdapter(PlayerActor, PlayerActorTransferAdapter)
          .addSpotMesh(SampleNames.roomSpotNode)
            .useAllocatedRoutingId(2, 'play')
            .setRoutingIdAllocationGroup('bingo.play')
            .enableRouter(config.playSpotEndpoint)
            .enablePubSub(config.playSpotPubSubEndpoint)
            .addEntrySpot(BingoEntrySpot)
            .addSpotFactory(BingoRoomSpot)
            .actorFactory(SampleNames.playerActorType, PlayerActorFactory)
            .useDrainPolicy('DrainNatural')
          .build();
        }
      })
    ],
    providers: [
      {
        provide: BINGO_MATCH_QUEUE,
        inject: [BINGO_SAMPLE_CONFIG],
        useFactory: (config: BingoSampleConfig) =>
          new RedisBingoMatchQueue(config.redisEndpoint, config.redisKeyPrefix)
      },
      AllocateBingoRoomHandler,
      AllocateBingoRoomSpotHandler,
      BingoRoomProvisioner,
      EnsurePlayerActorHandler,
      PlayerActorFactory,
      PlayerActorTransferAdapter,
      BingoRoomAllocator,
      BingoEntrySpot,
      MatchBingoActorHandler,
      ObserveBingoEventsHandler,
      StopObservingBingoEventsHandler,
      SubmitBingoCardHandler,
      BingoRoomTimerHandler,
      BingoRewardAcquiredEventHandler,
      PlayRouterReadinessHandler,
      RoomRouterReadinessHandler
    ]
  })(BingoPlayModule);

  return BingoPlayModule;
}

export { createBingoPlayModule };
