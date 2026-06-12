const { Module } = require('@nestjs/common');
const path = require('node:path');
const { ZLinkModule, zlinkDiscoverProviders, zlinkFramework } = require('../../../../../packages/nestjs/dist');
const { CreateGameEndpoint } = require('./Handlers/create-game-http-handler');
const { SampleNames } = require('../Configuration/sample-settings');

function createTicTacToeApiModule(config: {
  apiEndpoint: string;
  playEndpoint: string;
}) {
  class TicTacToeApiModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .clientServerChannel(SampleNames.apiChannel, (channel) => channel
            .server(config.apiEndpoint)
            .handlerGroup('api'))
          .clientServerChannel(SampleNames.playChannel, (channel) => channel
            .client(config.playEndpoint))
          .build()
      })
    ],
    providers: [
      CreateGameEndpoint,
      ...zlinkDiscoverProviders(path.join(__dirname, 'Handlers'))
    ]
  })(TicTacToeApiModule);

  return TicTacToeApiModule;
}

function getCreateGameEndpoint(app: { get(token: unknown, options?: { strict?: boolean }): unknown }): InstanceType<typeof CreateGameEndpoint> {
  return app.get(CreateGameEndpoint, { strict: false }) as InstanceType<typeof CreateGameEndpoint>;
}

export { createTicTacToeApiModule, getCreateGameEndpoint };
