import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { bingoFrameworkProtobuf } from '../../Shared/Contracts/protobuf-framework-codec';
import { SessionAuthenticator } from './Sessions/Handlers/authenticate-session-handler';
import { BingoSessionFactory } from './Sessions/bingo-session';
import { SampleNames } from '../Configuration/sample-names';
import { BINGO_SAMPLE_CONFIG, createBingoConfigurationModule } from '../Configuration/sample-config';
import type { BingoSampleConfig } from '../Configuration/sample-config';
import { bingoLocationOptions, createBingoLocationStore } from '../Configuration/location-store';
import { bingoMeterProvider } from '../runtime-support';
function createBingoSessionModule() {
  class BingoSessionModule {}
  const configuration = createBingoConfigurationModule([
    'sessionEndpoint',
    'sessionRouteEndpoint',
    'sessionSpotEndpoint',
    'sessionSpotNodeRid',
    'redisEndpoint',
    'redisKeyPrefix',
    'logDir'
  ]);

  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [BINGO_SAMPLE_CONFIG],
        useFactory: (endpoints: BingoSampleConfig) => {
          const builder = zlinkFramework();
          builder.options({ metrics: { meterProvider: bingoMeterProvider } });
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${endpoints.logDir}/flow-session.log`)
            .traceLabel('session');
          builder.addLocationStore(createBingoLocationStore(endpoints));
          Object.assign(builder.configureLocations(), bingoLocationOptions());
          return builder
          .codecs()
            .use(bingoFrameworkProtobuf)
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient()
          .addRouteMeshChannel(SampleNames.playChannel)
            .enableRouter(endpoints.sessionRouteEndpoint)
            .routingId(endpoints.sessionSpotNodeRid)
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
