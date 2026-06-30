import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createDispatchApiModule(config: DeliveryDispatchServerConfig) {
  class DispatchApiModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-dispatch-api.log`)
            .traceLabel('dispatch-api');
          return builder
            .useDiscovery()
              .addRegistryEndpoint(config.registryRouterEndpoint)
            .addClientServerChannel(SampleNames.dispatchChannel)
              .enableClient()
            .build();
        }
      })
    ]
  })(DispatchApiModule);

  return DispatchApiModule;
}

export { createDispatchApiModule };
