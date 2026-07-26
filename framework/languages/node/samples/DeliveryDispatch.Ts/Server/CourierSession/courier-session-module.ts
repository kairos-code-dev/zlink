import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { BindCourierSessionHandler, CourierSessionFactory } from './courier-session';
import { createDeliveryDispatchLocationStore, deliveryDispatchLocationOptions } from '../Configuration/location-store';
import {
  DELIVERYDISPATCH_SAMPLE_CONFIG,
  createDeliveryDispatchConfigurationModule
} from '../Configuration/sample-config';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createCourierSessionModule() {
  class CourierSessionModule {}
  const configuration = createDeliveryDispatchConfigurationModule([
    'courierStreamEndpoint',
    'courierSessionSpotEndpoint',
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
            .traceLogFile(`${config.logDir}/flow-courier-session.log`)
            .traceLabel('courier-session');
          builder.addLocationStore(createDeliveryDispatchLocationStore(config));
          deliveryDispatchLocationOptions(builder.configureLocations());
          const mesh = builder.addRouteMesh(SampleNames.routeMesh)
            .listen(config.courierSessionSpotEndpoint).useAllocatedRoutingId(16, 'delivery-session');
          mesh.channelName(SampleNames.routeMesh).setWeight(0);
          return builder.addStreamNode(SampleNames.courierStreamNode)
              .bind(config.courierStreamEndpoint)
              .registerSession(CourierSessionFactory)
            .build();
        }
      })
    ],
    providers: [
      {
        provide: 'DELIVERYDISPATCH_LOCATION_STORE',
        inject: [DELIVERYDISPATCH_SAMPLE_CONFIG],
        useFactory: (config: DeliveryDispatchServerConfig) => createDeliveryDispatchLocationStore(config)
      },
      BindCourierSessionHandler,
      CourierSessionFactory
    ]
  })(CourierSessionModule);

  return CourierSessionModule;
}

export { createCourierSessionModule };
