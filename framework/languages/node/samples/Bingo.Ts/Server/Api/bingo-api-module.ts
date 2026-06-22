import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { zlinkProtobufCodec } from '@zlink-systems/framework-codec-protobuf';
import { SampleNames } from '../Configuration/sample-names';
function createBingoApiModule(config: {
  apiEndpoint: string;
  registryRouterEndpoint: string;
}) {
  class BingoApiModule {}

  zlinkModule(__dirname, {
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.BINGO_LOG_DIR ?? 'logs'}/flow-api.log`)
            .traceNodeId('api');
          return builder
            .codecs()
              .use(zlinkProtobufCodec())
            .useDiscovery()
              .addRegistryEndpoint(config.registryRouterEndpoint)
            .addClientServerChannel(SampleNames.apiChannel)
              .enableServer(config.apiEndpoint)
              .addHandlerGroup('api')
            .addClientServerChannel(SampleNames.playChannel)
              .enableClient()
            .build();
        }
      })
    ]
  })(BingoApiModule);

  return BingoApiModule;
}

export { createBingoApiModule };
