import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { zlinkProtobufCodec } from '@zlink-systems/framework-codec-protobuf';
import { SessionAuthenticator } from './Sessions/Handlers/authenticate-session-handler';
import { BingoSessionFactory } from './Sessions/bingo-session';
import { SampleNames } from '../Configuration/sample-names';
import { BINGO_SAMPLE_CONFIG } from '../Configuration/sample-config';
import type { BingoSampleConfig } from '../Configuration/sample-config';
import { bingoLocationOptions, createBingoLocationStore } from '../Configuration/location-store';
function createBingoSessionModule(endpoints: {
  sessionEndpoint: string;
  sessionRouteEndpoint: string;
  sessionSpotEndpoint: string;
  sessionSpotNodeRid: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
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
            .traceLabel('session');
          builder.addLocationStore(createBingoLocationStore(endpoints));
          Object.assign(builder.configureLocations(), bingoLocationOptions());
          return builder
          .codecs()
            .use(zlinkProtobufCodec())
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient()
          .addRouteMeshChannel(SampleNames.playChannel)
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
