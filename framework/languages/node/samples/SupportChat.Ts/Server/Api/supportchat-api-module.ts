import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Configuration/sample-names';
import { createSupportChatLocationStore, supportChatLocationOptions } from '../Configuration/location-store';
import type { SupportChatServerConfig } from '../Configuration/sample-config';
import { AuthenticateUserHandler } from './Handlers/authenticate-user-handler';
import { OpenConversationHandler } from './Handlers/open-conversation-handler';

function createSupportChatApiModule(config: SupportChatServerConfig) {
  class SupportChatApiModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.SUPPORTCHAT_LOG_DIR ?? 'logs'}/flow-api.log`)
            .traceLabel('api');
          builder.addLocationStore(createSupportChatLocationStore(config));
          Object.assign(builder.configureLocations(), supportChatLocationOptions());
          return builder
            .addClientServerChannel(SampleNames.apiChannel)
              .enableServer(config.apiChannelEndpoint)
              .addHandlerGroup('api')
            .addClientServerChannel(SampleNames.supportChannel)
              .enableClient()
            .build();
        }
      })
    ],
    providers: [AuthenticateUserHandler, OpenConversationHandler]
  })(SupportChatApiModule);

  return SupportChatApiModule;
}

export { createSupportChatApiModule };
