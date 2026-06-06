require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkHandlerGroup } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../../../../shared/runtime-common');
const { AuthenticatePlayerHandler } = require('./Handlers/authenticate-player-handler');
const { MatchBingoHandler } = require('./Handlers/match-bingo-handler');

class BingoApiModule {}

Module({
  imports: [
    ZLinkModule.forRoot({
      clientServerChannels: {
        'bingo.api': {
          server: { bind: process.env.BINGO_API_ENDPOINT },
          handlerGroups: ['api']
        },
        'bingo.play': {
          client: { manualConnections: [process.env.BINGO_PLAY_ENDPOINT] }
        }
      }
    })
  ],
  providers: [
    ...zlinkHandlerGroup('api', [
      [AuthenticatePlayerHandler, 'AuthenticatePlayerReq'],
      [MatchBingoHandler, 'MatchBingoApiReq']
    ])
  ]
})(BingoApiModule);

async function bootstrap() {
  const app = await NestFactory.createApplicationContext(BingoApiModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: process.env.BINGO_API_ENDPOINT,
    channelName: 'bingo.api'
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await closeNestRuntime(app);
  }
}

bootstrap().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
