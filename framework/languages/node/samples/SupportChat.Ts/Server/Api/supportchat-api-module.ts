import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Configuration/sample-names';
import { createSupportChatLocationStore, supportChatLocationOptions } from '../Configuration/location-store';
import { SUPPORT_CHAT_CONFIG, createSupportChatConfigurationModule } from '../Configuration/sample-config';
import type { SupportChatServerConfig } from '../Configuration/sample-config';
import { AuthenticateUserHandler } from './Handlers/authenticate-user-handler';
import { OpenConversationHandler } from './Handlers/open-conversation-handler';

function createSupportChatApiModule() {
  class SupportChatApiModule {}
  const configuration = createSupportChatConfigurationModule([
    'apiChannelEndpoint', 'redisEndpoint', 'redisKeyPrefix', 'logDir'
  ]);

  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [SUPPORT_CHAT_CONFIG],
        useFactory: (config: SupportChatServerConfig) => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${config.logDir}/flow-api.log`)
            .traceLabel('api');
          builder.addLocationStore(createSupportChatLocationStore(config));
          supportChatLocationOptions(builder.configureLocations());
          const mesh = builder.addRouteMesh(SampleNames.conversationSpotMesh)
            .listen(config.apiChannelEndpoint)
            .useAllocatedRoutingId(16, 'support-api');
          mesh.channelName(SampleNames.apiChannel).addHandlerGroup('api');
          mesh.channelName(SampleNames.supportChannel).setWeight(0);
          mesh.channelName(SampleNames.conversationSpotMesh).setWeight(0);
          return builder.build();
        }
      })
    ],
    providers: [AuthenticateUserHandler, OpenConversationHandler]
  })(SupportChatApiModule);

  return SupportChatApiModule;
}

export { createSupportChatApiModule };
