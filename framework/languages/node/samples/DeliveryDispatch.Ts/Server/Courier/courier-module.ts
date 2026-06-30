import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { courierChannel } from '../../Shared/Configuration/sample-names';
import { OfferDeliveryHandler } from './offer-delivery-handler';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

type CourierOptions = {
  courierId: string;
  mode: string;
};

function createCourierModule(config: DeliveryDispatchServerConfig, options: CourierOptions) {
  class CourierModule {}
  const endpoint = options.courierId === 'courier-a' ? config.courierAEndpoint : config.courierBEndpoint;

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-${options.courierId}.log`)
            .traceLabel(options.courierId);
          return builder
            .useDiscovery()
              .addRegistryEndpoint(config.registryRouterEndpoint)
            .addClientServerChannel(courierChannel(options.courierId))
              .enableServer(endpoint)
              .addHandlerGroup('courier')
            .build();
        }
      })
    ],
    providers: [
      { provide: 'DELIVERYDISPATCH_COURIER_OPTIONS', useValue: options },
      OfferDeliveryHandler
    ]
  })(CourierModule);

  return CourierModule;
}

export {
  createCourierModule
};

export type {
  CourierOptions
};
