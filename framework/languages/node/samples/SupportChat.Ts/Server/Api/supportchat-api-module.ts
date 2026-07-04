import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Configuration/sample-names';
import type { SupportChatServerConfig } from '../Configuration/sample-config';

function createSupportChatApiModule(config: SupportChatServerConfig) {
  class SupportChatApiModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .addClientServerChannel(SampleNames.apiChannel)
            .enableServer(config.apiChannelEndpoint)
          .addClientServerChannel(SampleNames.supportChannel)
            .enableClient(config.supportChannelEndpoint)
          .build()
      })
    ]
  })(SupportChatApiModule);

  return SupportChatApiModule;
}

export { createSupportChatApiModule };
