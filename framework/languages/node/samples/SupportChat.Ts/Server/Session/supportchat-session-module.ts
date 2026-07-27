import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Configuration/sample-names';
import { createSupportChatLocationStore, supportChatLocationOptions } from '../Configuration/location-store';
import {
  AuthenticateSupportChatSessionHandler,
  CloseConversationSessionHandler,
  JoinConversationSessionHandler,
  OpenConversationSessionHandler,
  SendChatMessageSessionHandler,
  SetAgentAvailableSessionHandler,
  SetTypingSessionHandler,
  SupportChatSessionFactory,
  SupportChatSessionRouter
} from './Sessions/supportchat-session';
import { SUPPORT_CHAT_CONFIG, createSupportChatConfigurationModule } from '../Configuration/sample-config';
import type { SupportChatServerConfig } from '../Configuration/sample-config';

function createSupportChatSessionModule() {
  class SupportChatSessionModule {}
  const configuration = createSupportChatConfigurationModule([
    'sessionSpotEndpoint', 'sessionStreamEndpoint', 'redisEndpoint', 'redisKeyPrefix', 'logDir'
  ]);

  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [SUPPORT_CHAT_CONFIG],
        useFactory: (config: SupportChatServerConfig) => {
          const locationStore = createSupportChatLocationStore(config);
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${config.logDir}/flow-session.log`)
            .traceLabel('session');
          builder.addLocationStore(locationStore);
          supportChatLocationOptions(builder.configureLocations());
          const mesh = builder.addRouteMesh(SampleNames.conversationSpotMesh)
            .listen(config.sessionSpotEndpoint).setRoutingIdPrefix('support-session');
          mesh.channelName(SampleNames.apiChannel).setWeight(0);
          mesh.channelName(SampleNames.supportChannel).setWeight(0);
          mesh.channelName(SampleNames.conversationSpotMesh).setWeight(0);
          return builder.addStreamNode(SampleNames.sessionStreamNode)
              .bind(config.sessionStreamEndpoint)
              .registerSession(SupportChatSessionFactory as never)
            .build();
        }
      })
    ],
    providers: [
      {
        provide: 'SUPPORTCHAT_LOCATION_STORE',
        inject: [SUPPORT_CHAT_CONFIG],
        useFactory: (config: SupportChatServerConfig) => createSupportChatLocationStore(config)
      },
      SupportChatSessionRouter,
      AuthenticateSupportChatSessionHandler,
      OpenConversationSessionHandler,
      SetAgentAvailableSessionHandler,
      JoinConversationSessionHandler,
      SendChatMessageSessionHandler,
      SetTypingSessionHandler,
      CloseConversationSessionHandler,
      SupportChatSessionFactory
    ]
  })(SupportChatSessionModule);

  return SupportChatSessionModule;
}

export { createSupportChatSessionModule };
