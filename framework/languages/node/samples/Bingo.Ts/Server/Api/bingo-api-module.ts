const { Module } = require('@nestjs/common');
const path = require('node:path');
const { ZLinkModule, zlinkDiscoverProviders, zlinkFramework } = require('../../../../../packages/nestjs/dist');
const { SampleNames } = require('../Configuration/sample-names');

function createBingoApiModule(config: {
  apiEndpoint: string;
}, playEndpoint: string) {
  class BingoApiModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .clientServerChannel(SampleNames.apiChannel, (channel) => channel
            .server(config.apiEndpoint)
            .handlerGroup('api'))
          .clientServerChannel(SampleNames.playChannel, (channel) => channel
            .client(playEndpoint))
          .build()
      })
    ],
    providers: zlinkDiscoverProviders(path.join(__dirname, 'Handlers'))
  })(BingoApiModule);

  return BingoApiModule;
}

export { createBingoApiModule };
