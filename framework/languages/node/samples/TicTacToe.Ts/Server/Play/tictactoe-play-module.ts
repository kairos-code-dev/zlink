import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { PacketNames } from '../../Shared/Contracts/messages';
import { SampleNames } from '../Configuration/sample-settings';
import { CreateGameHandler } from './Infrastructure/ZLink/Handlers/create-game-handler';
import { PlayActorFactory } from './Infrastructure/ZLink/Actors/play-actor-factory';
import {
  DeliverPlayNotificationHandler,
  InitializePlayActorHandler
} from './Infrastructure/ZLink/Actors/play-actor';
import { PlayActorTransferAdapter } from './Infrastructure/ZLink/Actors/play-actor-transfer-adapter';
import { PlayActorJoinGameHandler } from './Infrastructure/ZLink/Spots/EntrySpot/Handlers/play-actor-join-game-handler';
import { PlayActorObserveMilestoneHandler } from './Infrastructure/ZLink/Spots/EntrySpot/Handlers/play-actor-observe-milestone-handler';
import { PlayActorLeaveGameHandler } from './Infrastructure/ZLink/Spots/TicTacToeGameSpot/Handlers/play-actor-leave-game-handler';
import { PlayActorPlaceMarkHandler } from './Infrastructure/ZLink/Spots/TicTacToeGameSpot/Handlers/play-actor-place-mark-handler';
import { PlayerWinMilestoneEventHandler } from './Infrastructure/ZLink/Spots/EntrySpot/Handlers/player-win-milestone-event-handler';
import { TicTacToeGameTimerHandler } from './Infrastructure/ZLink/Spots/TicTacToeGameSpot/Handlers/tictactoe-game-timer-handler';
import {
  DestroyPlayActorHandler,
  MilestoneObserverRegistry,
  PendingActorDestroyRegistry,
  PlayEntrySpot
} from './Infrastructure/ZLink/Spots/EntrySpot/play-entry-spot';
import {
  PlaceMarkAtGameSpotHandler,
  VerifyLeaveGameAtSpotHandler
} from './Infrastructure/ZLink/Spots/TicTacToeGameSpot/Handlers/tictactoe-game-operation-handlers';
import { TicTacToeGameSpot } from './Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe-game-spot';
import { TICTACTOE_GAME_ROOM_PROVISIONER, TicTacToeGameCreator } from './Application/GameCreation/tictactoe-game-creator';
import { ZLinkTicTacToeGameRoomProvisioner } from './Infrastructure/ZLink/tictactoe-game-room-provisioner';
import { PlaySessionFactory } from './Infrastructure/ZLink/Sessions/play-session-factory';
import { AuthenticatePlaySessionHandler } from './Infrastructure/ZLink/Sessions/Handlers/authenticate-play-session-handler';
import { PLAY_STREAM_ENDPOINT } from './play-tokens';
import { createTicTacToeLocationStore } from '../Configuration/location-store';
import { TICTACTOE_SAMPLE_CONFIG, createTicTacToeConfigurationModule } from '../Configuration/sample-config';
import type { TicTacToeSampleConfig } from '../Configuration/sample-config';
function createTicTacToePlayModule() {
  class TicTacToePlayModule {}
  const configuration = createTicTacToeConfigurationModule([
    'apiEndpoints',
    'playSpotEndpoint',
    'playStreamEndpoint',
    'playEndpoints',
    'redisEndpoint',
    'redisKeyPrefix',
    'playSpotNodeRid',
    'peerPlaySpotNodeRid',
    'peerPlaySpotEndpoint',
    'logDir'
  ]);

  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [TICTACTOE_SAMPLE_CONFIG],
        useFactory: (config: TicTacToeSampleConfig) => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${config.logDir}/flow-play-${config.playSpotNodeRid}.log`)
            .traceLabel(config.playSpotNodeRid);
          builder.addLocationStore(createTicTacToeLocationStore(config));
          builder.addStreamNode(SampleNames.playStream)
            .bind(config.playStreamEndpoint)
            .registerSession(PlaySessionFactory);
          const mesh = builder.addRouteMesh(SampleNames.playSpotNode)
            .listen(config.playSpotEndpoint)
            .routingId(config.playSpotNodeRid)
            .addEntrySpot(PlayEntrySpot)
            .addSpotFactory(TicTacToeGameSpot)
            .actorFactory(SampleNames.playerActorType, PlayActorFactory)
            .addActorTransferAdapter(SampleNames.playerActorType, PlayActorTransferAdapter);
          mesh.channelName(SampleNames.apiChannel).setWeight(0);
          mesh.channelName(SampleNames.playChannel)
            .addRequestHandler(PacketNames.createGameReq, CreateGameHandler);
          mesh.channelName(SampleNames.playSpotNode);
          mesh.channelName(SampleNames.playerMilestoneChannel);
          for (const endpoint of config.apiEndpoints) {
            mesh.peerConnections().connect(endpoint);
          }
          mesh.peerConnections().connect(config.peerPlaySpotNodeRid, config.peerPlaySpotEndpoint);
          return builder.build();
        }
      })
    ],
    providers: [
      {
        provide: PLAY_STREAM_ENDPOINT,
        inject: [TICTACTOE_SAMPLE_CONFIG],
        useFactory: (config: TicTacToeSampleConfig) => config.playStreamEndpoint
      },
      { provide: TICTACTOE_GAME_ROOM_PROVISIONER, useClass: ZLinkTicTacToeGameRoomProvisioner },
      TicTacToeGameCreator,
      CreateGameHandler,
      PlayActorFactory,
      PlayActorTransferAdapter,
      DeliverPlayNotificationHandler,
      InitializePlayActorHandler,
      MilestoneObserverRegistry,
      PendingActorDestroyRegistry,
      PlayEntrySpot,
      DestroyPlayActorHandler,
      PlayActorJoinGameHandler,
      PlayActorObserveMilestoneHandler,
      PlayActorLeaveGameHandler,
      PlayActorPlaceMarkHandler,
      PlayerWinMilestoneEventHandler,
      PlaySessionFactory,
      AuthenticatePlaySessionHandler,
      TicTacToeGameTimerHandler,
      PlaceMarkAtGameSpotHandler,
      VerifyLeaveGameAtSpotHandler,
    ]
  })(TicTacToePlayModule);

  return TicTacToePlayModule;
}

export { createTicTacToePlayModule };
