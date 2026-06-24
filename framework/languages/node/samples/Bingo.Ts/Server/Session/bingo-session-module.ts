import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { zlinkProtobufCodec } from '@zlink-systems/framework-codec-protobuf';
import { SessionAuthenticator } from './Sessions/Handlers/authenticate-session-handler';
import { BingoSessionFactory } from './Sessions/bingo-session';
import { SampleNames } from '../Configuration/sample-names';
import { BINGO_SAMPLE_CONFIG } from '../Configuration/sample-config';
import type { BingoSampleConfig } from '../Configuration/sample-config';
function createBingoSessionModule(endpoints: {
  registryRouterEndpoint: string;
  sessionEndpoint: string;
  sessionRouteEndpoint: string;
  sessionSpotEndpoint: string;
  sessionSpotNodeRid: string;
  preferredPlayNodeRid?: string;
  preferredPlayRouteEndpoint?: string;
} & Partial<BingoSampleConfig>) {
  class BingoSessionModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.BINGO_LOG_DIR ?? 'logs'}/flow-session.log`)
            .traceNodeId('session');
          return builder
          .options({
            registrySpotRemoteAddresses: {
              namespace: SampleNames.roomSpotNode,
              routerChannelId: SampleNames.roomRouteChannel
            }
          })
          .codecs()
            .use(zlinkProtobufCodec())
          .useDiscovery()
            .addRegistryEndpoint(endpoints.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient()
          .addClientServerChannel(SampleNames.playChannel)
            .enableClient()
          .addRouteMesh(SampleNames.roomRouteChannel)
            .enableRouter(endpoints.sessionRouteEndpoint)
            .routingId(endpoints.sessionSpotNodeRid)
            .connect(endpoints.preferredPlayRouteEndpoint)
          .addSpotMesh(SampleNames.roomSpotNode)
            .enableRouter(endpoints.sessionSpotEndpoint, endpoints.sessionSpotNodeRid)
          .addStreamNode(SampleNames.sessionStream)
            .bind(endpoints.sessionEndpoint)
            .registerSession(BingoSessionFactory)
          .build();
        }
      })
    ],
    providers: [
      { provide: BINGO_SAMPLE_CONFIG, useValue: endpoints },
      BingoSessionFactory,
      SessionAuthenticator
    ]
  })(BingoSessionModule);

  return BingoSessionModule;
}

function getSessionAuthenticator(app: { get(token: unknown, options?: { strict?: boolean }): unknown }): InstanceType<typeof SessionAuthenticator> {
  return app.get(SessionAuthenticator, { strict: false }) as InstanceType<typeof SessionAuthenticator>;
}

export { createBingoSessionModule, getSessionAuthenticator };
