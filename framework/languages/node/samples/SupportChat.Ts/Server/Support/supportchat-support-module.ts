import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Configuration/sample-names';
import type { SupportChatServerConfig } from '../Configuration/sample-config';

function createSupportChatSupportModule(config: SupportChatServerConfig) {
  class SupportChatSupportModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.SUPPORTCHAT_LOG_DIR ?? 'logs'}/flow-support.log`)
            .traceLabel('support');
          return builder
            .addClientServerChannel(SampleNames.supportChannel)
              .enableServer(config.supportChannelEndpoint)
            .addSpotMesh(SampleNames.conversationSpotMesh)
              .enableRouter(config.supportChannelEndpoint, 'support-node')
            .build();
        }
      })
    ],
    providers: []
  })(SupportChatSupportModule);

  return SupportChatSupportModule;
}

export { createSupportChatSupportModule };
