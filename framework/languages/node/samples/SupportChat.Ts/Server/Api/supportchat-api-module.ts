import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
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
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.SUPPORTCHAT_LOG_DIR ?? 'logs'}/flow-api.log`)
            .traceNodeId('api');
          return builder
          .codecs()
            .addJson()
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.apiChannel)
            .enableServer(config.apiEndpoint)
            .addHandlerGroup('api')
          .addClientServerChannel(SampleNames.supportChannel)
            .enableClient()
          .build();
        }
      })
    ]
  })(SupportChatApiModule);

  return SupportChatApiModule;
}

export { createSupportChatApiModule };
