import { Module } from '@nestjs/common';
import * as path from 'node:path';
import { ZLinkModule, zlinkDiscoverProviders, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames, SampleTimings } from '../Configuration/sample-names';
function createBingoApiModule(config: {
  apiEndpoint: string;
  registryRouterEndpoint: string;
}) {
  class BingoApiModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .options({ requestTimeoutMs: SampleTimings.requestTimeout })
          .codecs()
            .addProtobuf()
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.apiChannel)
            .enableServer(config.apiEndpoint)
            .addHandlerGroup('api')
          .addClientServerChannel(SampleNames.playChannel)
            .enableClient()
          .build()
      })
    ],
    providers: zlinkDiscoverProviders(path.join(__dirname, 'Handlers'))
  })(BingoApiModule);

  return BingoApiModule;
}

export { createBingoApiModule };
