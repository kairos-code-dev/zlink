import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { SupportChatSessionAuthenticator } from './Sessions/Handlers/authenticate-session-handler';
import { SupportChatSessionFactory } from './Sessions/supportchat-session';
import { SampleNames } from '../Configuration/sample-names';
function createSupportChatSessionModule(endpoints: {
  registryRouterEndpoint: string;
  sessionEndpoint: string;
}) {
  class SupportChatSessionModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.SUPPORTCHAT_LOG_DIR ?? 'logs'}/flow-session.log`)
            .traceLabel('session');
          return builder
          .useDiscovery()
            .addRegistryEndpoint(endpoints.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient()
          .addClientServerChannel(SampleNames.supportChannel)
            .enableClient()
          .addClientServerChannel(SampleNames.notificationChannel)
            .enableClient()
          .addStreamNode(SampleNames.sessionStream)
            .bind(endpoints.sessionEndpoint)
            .registerSession(SupportChatSessionFactory)
          .build();
        }
      })
    ],
    providers: [
      SupportChatSessionFactory,
      SupportChatSessionAuthenticator
    ]
  })(SupportChatSessionModule);

  return SupportChatSessionModule;
}

function getSessionAuthenticator(app: { get(token: unknown, options?: { strict?: boolean }): unknown }): InstanceType<typeof SupportChatSessionAuthenticator> {
  return app.get(SupportChatSessionAuthenticator, { strict: false }) as InstanceType<typeof SupportChatSessionAuthenticator>;
}

export { createSupportChatSessionModule, getSessionAuthenticator };
