require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, ZLINK_CHANNEL_CLIENT, zlinkFramework } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { SampleNames } = require('../Configuration/sample-settings');
const { loadSampleConfig } = require('../Configuration/sample-config');
const { PlayActorFactory } = require('./Adapters/ZLink/Actors/play-actor-factory');
const { PlayEntrySpot } = require('./Adapters/ZLink/Spots/play-entry-spot');
const { TicTacToeGameTimerHandler } = require('./Adapters/ZLink/Spots/Handlers/tictactoe-game-timer-handler');
const { TicTacToeGameCreator } = require('./Application/GameCreation/tictactoe-game-creator');
const { CreateGameHandler } = require('./Adapters/ZLink/Handlers/create-game-handler');
const { PlaySessionFactory } = require('./Adapters/ZLink/Sessions/play-session-factory');
const { PLAY_STREAM_ENDPOINT } = require('./play-tokens');

async function main(): Promise<void> {
  const config = loadSampleConfig();
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
      TicTacToeGameTimerHandler,
      PlaySessionFactory,
      { provide: 'TICTACTOE_API_CHANNEL_CLIENT', useExisting: ZLINK_CHANNEL_CLIENT },
      CreateGameHandler
    ]
  })(TicTacToePlayModule);

  const channelApp = await NestFactory.createApplicationContext(TicTacToePlayModule, {
    logger: false,
    abortOnError: false
  });
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.playEndpoint,
    streamEndpoint: config.playStreamEndpoint
  })}\n`);
  await waitForShutdown();
  await closeNestRuntime(channelApp);
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
