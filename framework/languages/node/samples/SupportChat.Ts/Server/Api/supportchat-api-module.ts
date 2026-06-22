import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { SampleNames } from '../Configuration/sample-names';
function createSupportChatApiModule(config: {
  apiEndpoint: string;
  supportEndpoint: string;
  registryRouterEndpoint: string;
}) {
  class SupportChatApiModule {}

  zlinkModule(__dirname, {
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .codecs()
            .addJson()
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.apiChannel)
            .enableServer(config.apiEndpoint)
            .addHandlerGroup('api')
          .addClientServerChannel(SampleNames.supportChannel)
            .enableClient(config.supportEndpoint)
          .build()
      })
    ]
  })(SupportChatApiModule);

  return SupportChatApiModule;
}

export { createSupportChatApiModule };
