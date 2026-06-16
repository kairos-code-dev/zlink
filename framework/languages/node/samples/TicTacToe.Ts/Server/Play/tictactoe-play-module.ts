import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { PacketNames } from '../../Shared/Contracts/messages';
import { SampleNames, SampleTimings } from '../Configuration/sample-settings';
import { CreateGameHandler } from './Adapters/ZLink/Handlers/create-game-handler';
import { PlayActorFactory } from './Adapters/ZLink/Actors/play-actor-factory';
import { PlayActorJoinGameHandler } from './Adapters/ZLink/Spots/Handlers/play-actor-join-game-handler';
import { PlayActorPlaceMarkHandler } from './Adapters/ZLink/Spots/Handlers/play-actor-place-mark-handler';
import { TicTacToeGameTimerHandler } from './Adapters/ZLink/Spots/Handlers/tictactoe-game-timer-handler';
import { PlayEntrySpot } from './Adapters/ZLink/Spots/play-entry-spot';
import { TicTacToeGameSpot } from './Adapters/ZLink/Spots/tictactoe-game-spot';
import { TicTacToeGameCreator } from './Application/GameCreation/tictactoe-game-creator';
import { PlaySessionFactory } from './Adapters/ZLink/Sessions/play-session-factory';
import { PLAY_STREAM_ENDPOINT } from './play-tokens';
function createTicTacToePlayModule(config: {
  apiEndpoint: string;
  playEndpoint: string;
  playSpotEndpoint: string;
  playStreamEndpoint: string;
}) {
  class TicTacToePlayModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .options({ requestTimeoutMs: SampleTimings.requestTimeout })
          .codecs()
            .addJson()
          .addClientServerChannel(SampleNames.playChannel)
            .enableServer(config.playEndpoint)
            .addRequestHandler(PacketNames.createGame, CreateGameHandler)
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient(config.apiEndpoint)
          .actorFactory(SampleNames.playerActorType, PlayActorFactory)
          .addStreamNode(SampleNames.playStream)
            .bind(config.playStreamEndpoint)
            .attachActorGateway(SampleNames.playSpotNode)
            .registerSession(PlaySessionFactory)
          .addSpotNode(SampleNames.playSpotNode)
            .enableRouter(config.playSpotEndpoint)
            .addEntrySpot(PlayEntrySpot)
            .addSpotFactory(TicTacToeGameSpot)
          .build()
      })
    ],
    providers: [
      { provide: PLAY_STREAM_ENDPOINT, useValue: config.playStreamEndpoint },
      TicTacToeGameCreator,
      CreateGameHandler,
      PlayActorFactory,
      PlayEntrySpot,
      PlayActorJoinGameHandler,
      PlayActorPlaceMarkHandler,
      PlaySessionFactory,
      TicTacToeGameTimerHandler,
    ]
  })(TicTacToePlayModule);

  return TicTacToePlayModule;
}

export { createTicTacToePlayModule };
