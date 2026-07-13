import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Configuration/sample-names';
import { createSupportChatLocationStore, supportChatLocationOptions } from '../Configuration/location-store';
import { SupportChatSessionFactory } from './Sessions/supportchat-session';
import type { SupportChatServerConfig } from '../Configuration/sample-config';

function createSupportChatSessionModule(config: SupportChatServerConfig) {
  class SupportChatSessionModule {}
  const locationStore = createSupportChatLocationStore(config);

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.SUPPORTCHAT_LOG_DIR ?? 'logs'}/flow-session.log`)
            .traceLabel('session');
          builder.addLocationStore(locationStore);
          Object.assign(builder.configureLocations(), supportChatLocationOptions());
          return builder
            .addClientServerChannel(SampleNames.apiChannel)
              .enableClient()
            .addClientServerChannel(SampleNames.supportChannel)
              .enableClient()
            .addSpotMesh(SampleNames.conversationSpotMesh)
              .enableRouter(config.sessionSpotEndpoint, 'session-node')
            .addStreamNode(SampleNames.sessionStreamNode)
              .bind(config.sessionStreamEndpoint)
              .registerSession(SupportChatSessionFactory as never)
            .build();
        }
      })
    ],
    providers: [
      { provide: 'SUPPORTCHAT_LOCATION_STORE', useValue: locationStore },
      SupportChatSessionFactory
    ]
  })(SupportChatSessionModule);

  return SupportChatSessionModule;
}

export { createSupportChatSessionModule };
