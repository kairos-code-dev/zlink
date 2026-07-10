import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { zlinkProtobufCodec } from '@zlink-systems/framework-codec-protobuf';
import { PlayerActorFactory } from './Infrastructure/ZLink/Actors/player-actor-factory';
import { PlayerActor } from './Infrastructure/ZLink/Actors/player-actor';
import { PlayerActorTransferAdapter } from './Infrastructure/ZLink/Actors/player-actor-transfer-adapter';
import { BingoEntrySpot } from './Infrastructure/ZLink/Spots/EntrySpot/bingo-entry-spot';
import { BingoRoomSpot } from './Infrastructure/ZLink/Spots/BingoRoomSpot/bingo-room-spot';
import { AllocateBingoRoomHandler } from './Infrastructure/ZLink/Handlers/allocate-bingo-room-handler';
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
import { BINGO_SAMPLE_CONFIG } from '../Configuration/sample-config';
import { bingoLocationOptions, createBingoLocationStore } from '../Configuration/location-store';
function createBingoPlayModule(config: {
	  playEndpoint: string;
	  playRouteEndpoint: string;
	  routePeerEndpoints: string[];
	  playSpotEndpoint: string;
	  playSpotPubSubEndpoint: string;
  playSpotPubSubPeerEndpoints: string[];
  playSpotNodeRid: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
}) {
  class BingoPlayModule {}

  zlinkModule(__dirname, {
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.BINGO_LOG_DIR ?? 'logs'}/flow-play.log`)
            .traceLabel('play');
          builder.addLocationStore(createBingoLocationStore(config));
          Object.assign(builder.configureLocations(), bingoLocationOptions());
          return builder
          .codecs()
            .use(zlinkProtobufCodec())
          .addRouteMeshChannel(SampleNames.playChannel)
            .enableRouter(config.playRouteEndpoint)
            .routingId(config.playSpotNodeRid)
            .connect(config.routePeerEndpoints)
            .addHandlerGroup('play')
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient()
          .addActorTransferAdapter(PlayerActor, PlayerActorTransferAdapter)
          .addSpotMesh(SampleNames.roomSpotNode)
            .enableRouter(config.playSpotEndpoint, config.playSpotNodeRid)
            .enablePubSub(config.playSpotPubSubEndpoint, config.playSpotNodeRid, config.playSpotPubSubPeerEndpoints)
            .addEntrySpot(BingoEntrySpot)
            .addSpotFactory(BingoRoomSpot)
            .actorFactory(SampleNames.playerActorType, PlayerActorFactory)
          .build();
        }
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
      PlayerActorTransferAdapter,
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
