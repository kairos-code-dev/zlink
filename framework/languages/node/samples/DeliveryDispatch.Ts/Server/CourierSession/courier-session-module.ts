import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { CourierSessionFactory } from './courier-session';
import { createDeliveryDispatchLocationStore, deliveryDispatchLocationOptions } from '../Configuration/location-store';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createCourierSessionModule(config: DeliveryDispatchServerConfig) {
  class CourierSessionModule {}
  const locationStore = createDeliveryDispatchLocationStore(config);

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-courier-session.log`)
            .traceLabel('courier-session');
          builder.addLocationStore(locationStore);
          Object.assign(builder.configureLocations(), deliveryDispatchLocationOptions());
          return builder
            .addRouteMeshChannel(SampleNames.courierActorNodeRouteChannel)
              .enableClient()
            .addStreamNode(SampleNames.courierStreamNode)
              .bind(config.courierStreamEndpoint)
              .registerSession(CourierSessionFactory)
            .addSpotMesh(SampleNames.courierActorSpotMesh)
              .enableRouter(config.courierSessionSpotEndpoint, 'courier-session')
            .build();
        }
      })
    ],
    providers: [
      { provide: 'DELIVERYDISPATCH_LOCATION_STORE', useValue: locationStore },
      CourierSessionFactory
    ]
  })(CourierSessionModule);

  return CourierSessionModule;
}

export { createCourierSessionModule };
