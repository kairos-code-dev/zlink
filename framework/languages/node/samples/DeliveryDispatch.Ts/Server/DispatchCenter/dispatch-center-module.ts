import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { AssignDeliveryHandler } from './Handlers/assign-delivery-handler';
import { DispatchWorkQueue } from './dispatch-work-queue';
import { DispatchWorker } from './dispatch-worker';
import { createDeliveryDispatchLocationStore, deliveryDispatchLocationOptions } from '../Configuration/location-store';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createDispatchCenterModule(config: DeliveryDispatchServerConfig) {
  class DispatchCenterModule {}
  const locationStore = createDeliveryDispatchLocationStore(config);

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-dispatch-center.log`)
            .traceLabel('dispatch-center');
          builder.addLocationStore(locationStore);
          Object.assign(builder.configureLocations(), deliveryDispatchLocationOptions());
          return builder
            .addClientServerChannel(SampleNames.dispatchChannel)
              .enableServer(config.dispatchEndpoint)
              .addHandlerGroup('dispatch')
            .addRouteMeshChannel(SampleNames.courierActorNodeRouteChannel)
              .enableClient()
            .addClientServerChannel(SampleNames.trackingChannel)
              .enableClient()
            .build();
        }
      })
    ],
    providers: [
      { provide: 'DELIVERYDISPATCH_LOCATION_STORE', useValue: locationStore },
      DispatchWorkQueue,
      DispatchWorker,
      AssignDeliveryHandler
    ]
  })(DispatchCenterModule);

  return DispatchCenterModule;
}

export { createDispatchCenterModule };
