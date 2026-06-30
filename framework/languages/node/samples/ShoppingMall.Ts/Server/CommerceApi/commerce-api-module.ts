import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { OrderStore } from '../Shared/Store/order-store';
import type { SampleConfig } from '../Configuration/sample-config';

function createCommerceApiModule(config: SampleConfig, instanceId: string) {
  class CommerceApiModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.SHOPPINGMALL_LOG_DIR ?? 'logs'}/flow-${instanceId}.log`)
            .traceLabel(instanceId);
          return builder
            .useDiscovery()
              .addRegistryEndpoint(config.registryRouterEndpoint)
          .addRouteMesh(SampleNames.orderWorkflowRouteChannel)
            .routingId(`${instanceId}-route`)
            .connect([config.workflowAEndpoint, config.workflowBEndpoint])
          .build();
        }
      })
    ],
    providers: [
      OrderStore
    ]
  })(CommerceApiModule);

  return CommerceApiModule;
}

export {
  createCommerceApiModule
};
