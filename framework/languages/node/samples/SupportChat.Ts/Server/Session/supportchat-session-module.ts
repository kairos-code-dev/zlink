import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Configuration/sample-names';
import { SupportChatSessionFactory } from './Sessions/supportchat-session';
import type { SupportChatServerConfig } from '../Configuration/sample-config';

function createSupportChatSessionModule(config: SupportChatServerConfig) {
  class SupportChatSessionModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .addStreamNode(SampleNames.sessionStreamNode)
            .bind(config.sessionStreamEndpoint)
            .registerSession(SupportChatSessionFactory as never)
          .build()
      })
    ],
    providers: [
      SupportChatSessionFactory
    ]
  })(SupportChatSessionModule);

  return SupportChatSessionModule;
}

export { createSupportChatSessionModule };
