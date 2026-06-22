import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SupportChatSessionAuthenticator } from './Sessions/Handlers/authenticate-session-handler';
import { SampleNames } from '../Configuration/sample-names';
function createSupportChatSessionModule(endpoints: {
  registryRouterEndpoint: string;
}) {
  class SupportChatSessionModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .codecs()
            .addJson()
          .useDiscovery()
            .addRegistryEndpoint(endpoints.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient()
          .addClientServerChannel(SampleNames.supportChannel)
            .enableClient()
          .addClientServerChannel(SampleNames.notificationChannel)
            .enableClient()
          .build()
      })
    ],
    providers: [
      SupportChatSessionAuthenticator
    ]
  })(SupportChatSessionModule);

  return SupportChatSessionModule;
}

function getSessionAuthenticator(app: { get(token: unknown, options?: { strict?: boolean }): unknown }): InstanceType<typeof SupportChatSessionAuthenticator> {
  return app.get(SupportChatSessionAuthenticator, { strict: false }) as InstanceType<typeof SupportChatSessionAuthenticator>;
}

export { createSupportChatSessionModule, getSessionAuthenticator };
