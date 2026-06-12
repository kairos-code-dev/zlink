const { Module } = require('@nestjs/common');
const path = require('node:path');
const { ZLinkModule, ZLINK_CHANNEL_CLIENT, zlinkDiscoverProviders, zlinkFramework } = require('../../../../../packages/nestjs/dist');
const { SampleNames } = require('../Configuration/sample-settings');
const { PlayActorFactory } = require('./Adapters/ZLink/Actors/play-actor-factory');
const { PlayEntrySpot } = require('./Adapters/ZLink/Spots/play-entry-spot');
const { TicTacToeGameCreator } = require('./Application/GameCreation/tictactoe-game-creator');
const { PlaySessionFactory } = require('./Adapters/ZLink/Sessions/play-session-factory');
const { PLAY_STREAM_ENDPOINT } = require('./play-tokens');

function createTicTacToePlayModule(config: {
  apiEndpoint: string;
  playEndpoint: string;
  playStreamEndpoint: string;
}) {
  class TicTacToePlayModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .clientServerChannel(SampleNames.playChannel, (channel) => channel
            .server(config.playEndpoint)
            .handlerGroup('play'))
          .clientServerChannel(SampleNames.apiChannel, (channel) => channel
            .client(config.apiEndpoint))
          .actorFactory(SampleNames.playerActorType, PlayActorFactory)
          .streamNode(SampleNames.playStream, (stream) => stream
            .bind(config.playStreamEndpoint)
            .registerSession(PlaySessionFactory))
          .spotNode(SampleNames.playSpotNode, (spot) => spot
            .entrySpot(PlayEntrySpot))
          .build()
      })
    ],
    providers: [
      { provide: PLAY_STREAM_ENDPOINT, useValue: config.playStreamEndpoint },
      TicTacToeGameCreator,
      PlayActorFactory,
      PlayEntrySpot,
      PlaySessionFactory,
      { provide: 'TICTACTOE_API_CHANNEL_CLIENT', useExisting: ZLINK_CHANNEL_CLIENT },
      ...zlinkDiscoverProviders(path.join(__dirname, 'Adapters', 'ZLink', 'Handlers')),
      ...zlinkDiscoverProviders(path.join(__dirname, 'Adapters', 'ZLink', 'Spots', 'Handlers'))
    ]
  })(TicTacToePlayModule);

  return TicTacToePlayModule;
}

export { createTicTacToePlayModule };
