require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkFramework, zlinkHandlers } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { createRegistryClient } = require('../discovery-support');
const { AuthenticatePlayerHandler } = require('./Handlers/authenticate-player-handler');
const { MatchBingoHandler } = require('./Handlers/match-bingo-handler');
const { SampleNames } = require('../../Shared/Configuration/sample-names');
const { PacketNames } = require('../../Shared/Contracts/messages');

async function bootstrap(): Promise<void> {
  const registry = await createRegistryClient(process.env.BINGO_REGISTRY_ENDPOINT);
  const play = await registry.resolve(SampleNames.playService);

  class BingoApiModule {}

  Module({
    imports: [
      ZLinkModule.forRoot(
        zlinkFramework()
          .clientServerChannel(SampleNames.apiChannel, (channel) => channel
            .server(process.env.BINGO_API_ENDPOINT)
            .handlerGroup('api'))
          .clientServerChannel(SampleNames.playChannel, (channel) => channel
            .client(play.endpoint))
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

  const app = await NestFactory.createApplicationContext(BingoApiModule, {
    logger: false,
    abortOnError: false
  });
  await registry.register(SampleNames.apiService, process.env.BINGO_API_ENDPOINT);

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: process.env.BINGO_API_ENDPOINT,
    channelName: 'bingo.api'
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await registry.stop();
    await closeNestRuntime(app);
  }
}

bootstrap().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
