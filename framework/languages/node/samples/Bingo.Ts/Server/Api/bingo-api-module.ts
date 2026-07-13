import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { bingoFrameworkProtobuf } from '../../Shared/Contracts/protobuf-framework-codec';
import { SampleNames } from '../Configuration/sample-names';
import { bingoLocationOptions, createBingoLocationStore } from '../Configuration/location-store';
import { bingoMeterProvider } from '../runtime-support';
function createBingoApiModule(config: {
  apiEndpoint: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
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
              .use(bingoFrameworkProtobuf)
            .addClientServerChannel(SampleNames.apiChannel)
              .enableServer(config.apiEndpoint)
              .addHandlerGroup('api')
            .addRouteMeshChannel(SampleNames.playChannel)
              .enableClient()
            .build();
        }
      })
    ]
  })(BingoApiModule);

  return BingoApiModule;
}

export { createBingoApiModule };
