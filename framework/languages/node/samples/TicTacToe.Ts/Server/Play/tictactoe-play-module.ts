import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { PacketNames } from '../../Shared/Contracts/messages';
import { SampleNames } from '../Configuration/sample-settings';
import { CreateGameHandler } from './Infrastructure/ZLink/Handlers/create-game-handler';
import { PlayActorFactory } from './Infrastructure/ZLink/Actors/play-actor-factory';
import { PlayActor } from './Infrastructure/ZLink/Actors/play-actor';
import { PlayActorTransferAdapter } from './Infrastructure/ZLink/Actors/play-actor-transfer-adapter';
import { PlayActorJoinGameHandler } from './Infrastructure/ZLink/Spots/EntrySpot/Handlers/play-actor-join-game-handler';
import { PlayActorLeaveGameHandler } from './Infrastructure/ZLink/Spots/TicTacToeGameSpot/Handlers/play-actor-leave-game-handler';
import { PlayActorPlaceMarkHandler } from './Infrastructure/ZLink/Spots/TicTacToeGameSpot/Handlers/play-actor-place-mark-handler';
import { PlayerWinMilestoneEventHandler } from './Infrastructure/ZLink/Spots/EntrySpot/Handlers/player-win-milestone-event-handler';
import { TicTacToeGameTimerHandler } from './Infrastructure/ZLink/Spots/TicTacToeGameSpot/Handlers/tictactoe-game-timer-handler';
import { MilestoneObserverRegistry, PlayEntrySpot } from './Infrastructure/ZLink/Spots/EntrySpot/play-entry-spot';
import { TicTacToeGameSpot } from './Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe-game-spot';
import { TICTACTOE_GAME_ROOM_PROVISIONER, TicTacToeGameCreator } from './Application/GameCreation/tictactoe-game-creator';
import { ZLinkTicTacToeGameRoomProvisioner } from './Infrastructure/ZLink/tictactoe-game-room-provisioner';
import { PlaySessionFactory } from './Infrastructure/ZLink/Sessions/play-session-factory';
import { PLAY_STREAM_ENDPOINT } from './play-tokens';
import {
  createTicTacToeLocationStore,
  RedisRoomRouteStore,
  TICTACTOE_SAMPLE_CONFIG
} from '../Configuration/redis-room-route-store';
function createTicTacToePlayModule(config: {
  apiEndpoints: string[];
  playEndpoint: string;
  playEndpoints: string[];
  playSpotEndpoint: string;
  playSpotPubSubEndpoint: string;
  playStreamEndpoint: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
  playSpotNodeRid: string;
  peerPlaySpotNodeRid: string;
  peerPlaySpotEndpoint: string;
  peerPlaySpotPubEndpoint: string;
}) {
  class TicTacToePlayModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.TICTACTOE_LOG_DIR ?? 'logs'}/flow-play-${config.playSpotNodeRid}.log`)
            .traceLabel(config.playSpotNodeRid);
          builder.addLocationStore(createTicTacToeLocationStore(config));
          return builder
          .addClientServerChannel(SampleNames.playChannel)
            .enableServer(config.playEndpoint)
            .addRequestHandler(PacketNames.createGameReq, CreateGameHandler)
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient(config.apiEndpoints)
            .addActorTransferAdapter(PlayActor, PlayActorTransferAdapter)
            .addStreamNode(SampleNames.playStream)
              .bind(config.playStreamEndpoint)
            .registerSession(PlaySessionFactory)
            .addSpotMesh(SampleNames.playSpotNode)
              .enableRouter(config.playSpotEndpoint, config.playSpotNodeRid)
              .connectRouter(config.peerPlaySpotNodeRid, config.peerPlaySpotEndpoint)
              .enablePubSub(config.playSpotPubSubEndpoint, config.playSpotNodeRid)
              .connectPeerPub(config.peerPlaySpotPubEndpoint)
              .addEntrySpot(PlayEntrySpot)
            .addSpotFactory(TicTacToeGameSpot)
            .actorFactory(SampleNames.playerActorType, PlayActorFactory)
          .build();
        }
      })
    ],
    providers: [
      { provide: TICTACTOE_SAMPLE_CONFIG, useValue: config },
      { provide: PLAY_STREAM_ENDPOINT, useValue: config.playStreamEndpoint },
      RedisRoomRouteStore,
      { provide: TICTACTOE_GAME_ROOM_PROVISIONER, useClass: ZLinkTicTacToeGameRoomProvisioner },
      TicTacToeGameCreator,
      CreateGameHandler,
      PlayActorFactory,
      PlayActorTransferAdapter,
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
