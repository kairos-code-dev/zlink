import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Configuration/sample-names';
import { createSupportChatLocationStore, supportChatLocationOptions } from '../Configuration/location-store';
import { SupportChatSessionFactory } from './Sessions/supportchat-session';
import type { SupportChatServerConfig } from '../Configuration/sample-config';

function createSupportChatSessionModule(config: SupportChatServerConfig) {
  class SupportChatSessionModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.addLocationStore(createSupportChatLocationStore(config));
          Object.assign(builder.configureLocations(), supportChatLocationOptions());
          return builder
            .addStreamNode(SampleNames.sessionStreamNode)
              .bind(config.sessionStreamEndpoint)
              .registerSession(SupportChatSessionFactory as never)
            .build();
        }
      })
    ],
    providers: [
      SupportChatSessionFactory
    ]
  })(SupportChatSessionModule);

  return SupportChatSessionModule;
}

export { createSupportChatSessionModule };
