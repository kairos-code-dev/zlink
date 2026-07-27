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
import { RoomRouterReadinessHandler } from '../Configuration/room-router-readiness-handler';
function createBingoSessionModule() {
  class BingoSessionModule {}
  const configuration = createBingoConfigurationModule([
    'sessionEndpoint',
    'sessionSpotEndpoint',
    'sessionSpotPubSubEndpoint',
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
          builder.options({
            metrics: { meterProvider: bingoMeterProvider },
            monitoring: {}
          });
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${endpoints.logDir}/flow-session.log`)
            .traceLabel('session');
          builder.addLocationStore(createBingoLocationStore(endpoints));
          bingoLocationOptions(builder.configureLocations());
          builder.codecs().use(bingoFrameworkProtobuf);
          const mesh = builder.addRouteMesh(SampleNames.roomSpotNode)
            .setRoutingIdPrefix('session')
            .listen(endpoints.sessionSpotEndpoint);
          mesh.channelName(SampleNames.apiChannel).setWeight(0);
          mesh.channelName(SampleNames.roomSpotNode).setWeight(0);
          return builder.addStreamNode(SampleNames.sessionStream)
            .enableActorDispatch(SampleNames.roomSpotNode)
            .bind(endpoints.sessionEndpoint)
            .registerSession(BingoSessionFactory)
          .build();
        }
      })
    ],
    providers: [
      BingoSessionFactory,
      SessionAuthenticator,
      RoomRouterReadinessHandler
    ]
  })(BingoSessionModule);

  return BingoSessionModule;
}

function getSessionAuthenticator(app: { get(token: unknown, options?: { strict?: boolean }): unknown }): InstanceType<typeof SessionAuthenticator> {
  return app.get(SessionAuthenticator, { strict: false }) as InstanceType<typeof SessionAuthenticator>;
}

export { createBingoSessionModule, getSessionAuthenticator };
