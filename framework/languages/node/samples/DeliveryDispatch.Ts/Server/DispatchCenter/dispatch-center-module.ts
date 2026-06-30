import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { AssignDeliveryHandler } from './Handlers/assign-delivery-handler';
import { DispatchWorkQueue } from './dispatch-work-queue';
import { DispatchWorker } from './dispatch-worker';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createDispatchCenterModule(config: DeliveryDispatchServerConfig) {
  class DispatchCenterModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-dispatch-center.log`)
            .traceLabel('dispatch-center');
          return builder
            .useDiscovery()
              .addRegistryEndpoint(config.registryRouterEndpoint)
            .addClientServerChannel(SampleNames.dispatchChannel)
              .enableServer(config.dispatchEndpoint)
              .addHandlerGroup('dispatch')
            .addClientServerChannel(SampleNames.courierAChannel)
              .enableClient()
            .addClientServerChannel(SampleNames.courierBChannel)
              .enableClient()
            .addClientServerChannel(SampleNames.trackingChannel)
              .enableClient()
            .build();
        }
      })
    ],
    providers: [
      DispatchWorkQueue,
      DispatchWorker,
      AssignDeliveryHandler
    ]
  })(DispatchCenterModule);

  return DispatchCenterModule;
}

export { createDispatchCenterModule };
