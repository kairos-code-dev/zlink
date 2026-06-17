import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { SampleNames, SampleTimings } from '../Configuration/sample-names';
function createBingoApiModule(config: {
  apiEndpoint: string;
  registryRouterEndpoint: string;
}) {
  class BingoApiModule {}

  zlinkModule(__dirname, {
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
    ]
  })(BingoApiModule);

  return BingoApiModule;
}

export { createBingoApiModule };
