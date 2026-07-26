import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { CustomerSessionFactory, SubscribeDeliverySessionHandler } from './customer-session';
import { CustomerActorDirectory, CustomerActorFactory } from './customer-actor';
import { CustomerEntrySpot } from './customer-entry-spot';
import { CustomerStatusHandler } from './customer-status-handler';
import { createDeliveryDispatchLocationStore, deliveryDispatchLocationOptions } from '../Configuration/location-store';
import {
  DELIVERYDISPATCH_SAMPLE_CONFIG,
  createDeliveryDispatchConfigurationModule
} from '../Configuration/sample-config';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createSessionModule() {
  class SessionModule {}
  const directory = new CustomerActorDirectory();
  CustomerActorFactory.useDirectory(directory);
  const configuration = createDeliveryDispatchConfigurationModule([
    'sessionSpotRouterEndpoint',
    'sessionStreamEndpoint',
    'redisEndpoint',
    'redisKeyPrefix',
    'logDir'
  ]);

  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [DELIVERYDISPATCH_SAMPLE_CONFIG],
        useFactory: (config: DeliveryDispatchServerConfig) => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${config.logDir}/flow-customer-gateway.log`)
            .traceLabel('customer-gateway');
          builder.addLocationStore(createDeliveryDispatchLocationStore(config));
          deliveryDispatchLocationOptions(builder.configureLocations());
          const mesh = builder.addRouteMesh(SampleNames.routeMesh)
              .listen(config.sessionSpotRouterEndpoint).useAllocatedRoutingId(16, 'delivery-customer')
              .addEntrySpot(CustomerEntrySpot)
              .actorFactory(SampleNames.customerActorType, CustomerActorFactory);
          mesh.channelName(SampleNames.routeMesh);
          return builder.addStreamNode(SampleNames.customerStreamNode)
              .bind(config.sessionStreamEndpoint)
              .registerSession(CustomerSessionFactory)
            .build();
        }
      })
    ],
    providers: [
      { provide: CustomerActorDirectory, useValue: directory },
      {
        provide: 'DELIVERYDISPATCH_LOCATION_STORE',
        inject: [DELIVERYDISPATCH_SAMPLE_CONFIG],
        useFactory: (config: DeliveryDispatchServerConfig) => createDeliveryDispatchLocationStore(config)
      },
      SubscribeDeliverySessionHandler,
      CustomerSessionFactory,
      CustomerActorFactory,
      CustomerEntrySpot,
      CustomerStatusHandler
    ]
  })(SessionModule);

  return SessionModule;
}

export { createSessionModule };
