import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { PacketNames } from '../../Shared/Contracts/messages';
import { SampleNames, SampleTimings } from '../Configuration/sample-settings';
import { CreateGameHandler } from './Adapters/ZLink/Handlers/create-game-handler';
import { PlayActorFactory } from './Adapters/ZLink/Actors/play-actor-factory';
import { PlayActorJoinGameHandler } from './Adapters/ZLink/Spots/Handlers/play-actor-join-game-handler';
import { PlayActorLeaveGameHandler } from './Adapters/ZLink/Spots/Handlers/play-actor-leave-game-handler';
import { PlayActorPlaceMarkHandler } from './Adapters/ZLink/Spots/Handlers/play-actor-place-mark-handler';
import { PlayerWinMilestoneEventHandler } from './Adapters/ZLink/Spots/Handlers/player-win-milestone-event-handler';
import { TicTacToeGameTimerHandler } from './Adapters/ZLink/Spots/Handlers/tictactoe-game-timer-handler';
import { MilestoneObserverRegistry, PlayEntrySpot } from './Adapters/ZLink/Spots/play-entry-spot';
import { TicTacToeGameSpot } from './Adapters/ZLink/Spots/tictactoe-game-spot';
import { TicTacToeGameCreator } from './Application/GameCreation/tictactoe-game-creator';
import { PlaySessionFactory } from './Adapters/ZLink/Sessions/play-session-factory';
import { PLAY_STREAM_ENDPOINT } from './play-tokens';
import {
  RedisRoomRouteStore,
  RedisSpotRemoteAddressResolver,
  TICTACTOE_SAMPLE_CONFIG
} from '../Configuration/redis-room-route-store';
function createTicTacToePlayModule(config: {
  apiEndpoints: string[];
  playEndpoint: string;
  playEndpoints: string[];
  playSpotEndpoint: string;
  playSpotPubSubEndpoint: string;
  playRouteEndpoint: string;
  playStreamEndpoint: string;
  playSpotNodeRid: string;
  peerPlaySpotNodeRid: string;
  peerPlaySpotEndpoint: string;
  peerPlayRouteEndpoint: string;
  peerPlaySpotPubEndpoint: string;
}) {
  class TicTacToePlayModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .options({
            requestTimeoutMs: SampleTimings.requestTimeout,
            spotRemoteAddressResolver: RedisSpotRemoteAddressResolver
          })
          .codecs()
            .addJson()
          .addClientServerChannel(SampleNames.playChannel)
            .enableServer(config.playEndpoint)
            .addRequestHandler(PacketNames.createGame, CreateGameHandler)
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient(config.apiEndpoints)
            .actorFactory(SampleNames.playerActorType, PlayActorFactory)
            .addStreamNode(SampleNames.playStream)
              .bind(config.playStreamEndpoint)
              .attachActorGateway(SampleNames.playSpotNode)
            .registerSession(PlaySessionFactory)
            .addSpotNode(SampleNames.playSpotNode)
              .enableRouter(config.playSpotEndpoint, config.playSpotNodeRid)
              .connectRouter(config.peerPlaySpotNodeRid, config.peerPlaySpotEndpoint)
              .enablePubSub(config.playSpotPubSubEndpoint, config.playSpotNodeRid)
              .connectPeerPub(config.peerPlaySpotPubEndpoint)
              .attachSpotPublisherClient(SampleNames.playerMilestoneChannel, config.playSpotPubSubEndpoint)
              .addEntrySpot(PlayEntrySpot)
            .addSpotFactory(TicTacToeGameSpot)
          .build()
      })
    ],
    providers: [
      { provide: TICTACTOE_SAMPLE_CONFIG, useValue: config },
      { provide: PLAY_STREAM_ENDPOINT, useValue: config.playStreamEndpoint },
      RedisRoomRouteStore,
      RedisSpotRemoteAddressResolver,
      TicTacToeGameCreator,
      CreateGameHandler,
      PlayActorFactory,
      MilestoneObserverRegistry,
      PlayEntrySpot,
      PlayActorJoinGameHandler,
      PlayActorLeaveGameHandler,
      PlayActorPlaceMarkHandler,
      PlayerWinMilestoneEventHandler,
      PlaySessionFactory,
      TicTacToeGameTimerHandler,
    ]
  })(TicTacToePlayModule);

  return TicTacToePlayModule;
}

export { createTicTacToePlayModule };
