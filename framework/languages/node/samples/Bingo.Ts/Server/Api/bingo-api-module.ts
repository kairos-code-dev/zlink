import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { bingoFrameworkProtobuf } from '../../Shared/Contracts/protobuf-framework-codec';
import { SampleNames } from '../Configuration/sample-names';
import { BINGO_SAMPLE_CONFIG, createBingoConfigurationModule } from '../Configuration/sample-config';
import type { BingoSampleConfig } from '../Configuration/sample-config';
import { bingoLocationOptions, createBingoLocationStore } from '../Configuration/location-store';
import { bingoMeterProvider } from '../runtime-support';
import { BingoPlayerRecordStore } from './Handlers/player-record-handlers';
function createBingoApiModule() {
  class BingoApiModule {}
  const configuration = createBingoConfigurationModule([
    'apiEndpoint',
    'redisEndpoint',
    'redisKeyPrefix',
    'logDir'
  ]);

  zlinkModule(__dirname, {
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [BINGO_SAMPLE_CONFIG],
        useFactory: (config: BingoSampleConfig) => {
          const builder = zlinkFramework();
          builder.options({ metrics: { meterProvider: bingoMeterProvider } });
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${config.logDir}/flow-api.log`)
            .traceLabel('api');
          builder.addLocationStore(createBingoLocationStore(config));
          Object.assign(builder.configureLocations(), bingoLocationOptions());
          return builder
            .codecs()
              .use(bingoFrameworkProtobuf)
            .addClientServerChannel(SampleNames.apiChannel)
              .useAllocatedRoutingId(2, 'api')
              .setRoutingIdAllocationGroup('bingo.api')
              .enableServer(config.apiEndpoint)
              .addHandlerGroup('api')
            .addClientServerChannel(SampleNames.playChannel)
              .enableClient()
            .build();
        }
      })
    ],
    providers: [BingoPlayerRecordStore]
  })(BingoApiModule);

  return BingoApiModule;
}

export { createBingoApiModule };
