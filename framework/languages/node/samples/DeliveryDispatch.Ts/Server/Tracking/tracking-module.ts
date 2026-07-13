import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { EvidenceStore } from '../Configuration/evidence-store';
import {
  DeliveryStatusChangedHandler
} from './Handlers/tracking-handlers';
import { createDeliveryDispatchLocationStore, deliveryDispatchLocationOptions } from '../Configuration/location-store';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createTrackingModule(config: DeliveryDispatchServerConfig, evidence: EvidenceStore) {
  class TrackingModule {}
  const locationStore = createDeliveryDispatchLocationStore(config);

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-tracking.log`)
            .traceLabel('tracking');
          builder.addLocationStore(locationStore);
          Object.assign(builder.configureLocations(), deliveryDispatchLocationOptions());
          return builder
            .addClientServerChannel(SampleNames.trackingChannel)
              .enableServer(config.trackingEndpoint)
              .addHandlerGroup('tracking')
            .addSpotMesh(SampleNames.customerActorSpotMesh)
              .enableRouter(config.trackingSpotEndpoint, 'tracking-customer-relay')
            .build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      { provide: 'DELIVERYDISPATCH_LOCATION_STORE', useValue: locationStore },
      DeliveryStatusChangedHandler
    ]
  })(TrackingModule);

  return TrackingModule;
}

export { createTrackingModule };
