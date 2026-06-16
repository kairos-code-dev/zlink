import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SessionAuthenticator } from './Sessions/Handlers/authenticate-session-handler';
import { SampleNames, SampleTimings } from '../Configuration/sample-names';
function createBingoSessionModule(endpoints: {
  registryRouterEndpoint: string;
}) {
  class BingoSessionModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .options({ requestTimeoutMs: SampleTimings.requestTimeout })
          .codecs()
            .addProtobuf()
          .useDiscovery()
            .addRegistryEndpoint(endpoints.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient()
          .addClientServerChannel(SampleNames.playChannel)
            .enableClient()
          .addClientServerChannel(SampleNames.notificationChannel)
            .enableClient()
          .build()
      })
    ],
    providers: [
      SessionAuthenticator
    ]
  })(BingoSessionModule);

  return BingoSessionModule;
}

function getSessionAuthenticator(app: { get(token: unknown, options?: { strict?: boolean }): unknown }): InstanceType<typeof SessionAuthenticator> {
  return app.get(SessionAuthenticator, { strict: false }) as InstanceType<typeof SessionAuthenticator>;
}

export { createBingoSessionModule, getSessionAuthenticator };
