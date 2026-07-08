import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Configuration/sample-names';
import { createSupportChatLocationStore, supportChatLocationOptions } from '../Configuration/location-store';
import type { SupportChatServerConfig } from '../Configuration/sample-config';

function createSupportChatApiModule(config: SupportChatServerConfig) {
  class SupportChatApiModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.addLocationStore(createSupportChatLocationStore(config));
          Object.assign(builder.configureLocations(), supportChatLocationOptions());
          return builder
            .addClientServerChannel(SampleNames.apiChannel)
              .enableServer(config.apiChannelEndpoint)
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
