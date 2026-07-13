import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { CustomerSessionFactory } from './customer-session';
import { CustomerActorDirectory, CustomerActorFactory } from './customer-actor';
import { CustomerEntrySpot } from './customer-entry-spot';
import { CustomerStatusHandler } from './customer-status-handler';
import { createDeliveryDispatchLocationStore, deliveryDispatchLocationOptions } from '../Configuration/location-store';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createSessionModule(config: DeliveryDispatchServerConfig) {
  class SessionModule {}
  const directory = new CustomerActorDirectory();
  const locationStore = createDeliveryDispatchLocationStore(config);
  CustomerActorFactory.useDirectory(directory);

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-customer-gateway.log`)
            .traceLabel('customer-gateway');
          builder.addLocationStore(locationStore);
          Object.assign(builder.configureLocations(), deliveryDispatchLocationOptions());
          return builder
            .addSpotMesh(SampleNames.customerActorSpotMesh)
              .enableRouter(config.sessionSpotRouterEndpoint, config.sessionSpotNodeRid)
              .addEntrySpot(CustomerEntrySpot)
              .actorFactory(SampleNames.customerActorType, CustomerActorFactory)
            .addStreamNode(SampleNames.customerStreamNode)
              .bind(config.sessionStreamEndpoint)
              .registerSession(CustomerSessionFactory)
            .build();
        }
      })
    ],
    providers: [
      { provide: CustomerActorDirectory, useValue: directory },
      { provide: 'DELIVERYDISPATCH_CUSTOMER_SPOT_RID', useValue: config.sessionSpotNodeRid },
      { provide: 'DELIVERYDISPATCH_LOCATION_STORE', useValue: locationStore },
      CustomerSessionFactory,
      CustomerActorFactory,
      CustomerEntrySpot,
      CustomerStatusHandler
    ]
  })(SessionModule);

  return SessionModule;
}

export { createSessionModule };
