require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkFramework, zlinkHandlers } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { AuthenticatePlayerHandler } = require('./Handlers/authenticate-player-handler');
const { MatchBingoHandler } = require('./Handlers/match-bingo-handler');
const { PacketNames } = require('../../Shared/Contracts/messages');

class BingoApiModule {}

Module({
  imports: [
    ZLinkModule.forRoot(
      zlinkFramework()
        .clientServerChannel('bingo.api', (channel) => channel
          .server(process.env.BINGO_API_ENDPOINT)
          .handlerGroup('api'))
        .clientServerChannel('bingo.play', (channel) => channel
          .client(process.env.BINGO_PLAY_ENDPOINT))
        .build()
    )
  ],
  providers: [
    ...zlinkHandlers('api')
      .request(AuthenticatePlayerHandler, PacketNames.authenticatePlayerReq)
      .request(MatchBingoHandler, PacketNames.matchBingoApiReq)
      .providers()
  ]
})(BingoApiModule);

async function bootstrap(): Promise<void> {
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

bootstrap().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
