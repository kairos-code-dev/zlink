import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { zlinkProtobufCodec } from '@zlink-systems/framework-codec-protobuf';
import { SampleNames } from '../Configuration/sample-names';
import { bingoLocationOptions, createBingoLocationStore } from '../Configuration/location-store';
import { bingoMeterProvider } from '../runtime-support';
function createBingoApiModule(config: {
  apiEndpoint: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
  apiNodeRid?: string;
  playRouteEndpoints?: readonly string[];
}) {
  class BingoApiModule {}

  zlinkModule(__dirname, {
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.options({ metrics: { meterProvider: bingoMeterProvider } });
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.BINGO_LOG_DIR ?? 'logs'}/flow-api.log`)
            .traceLabel('api');
          builder.addLocationStore(createBingoLocationStore(config));
          Object.assign(builder.configureLocations(), bingoLocationOptions());
          return builder
            .codecs()
              .use(zlinkProtobufCodec())
            .addClientServerChannel(SampleNames.apiChannel)
              .enableServer(config.apiEndpoint)
              .addHandlerGroup('api')
            .addRouteMeshChannel(SampleNames.playChannel)
              .routingId(config.apiNodeRid ?? 'bingo-api-node')
              .connect(config.playRouteEndpoints)
            .build();
        }
      })
    ]
  })(BingoApiModule);

  return BingoApiModule;
}

export { createBingoApiModule };
