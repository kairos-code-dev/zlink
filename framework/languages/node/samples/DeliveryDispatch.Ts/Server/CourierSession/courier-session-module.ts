import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { CourierSessionFactory } from './courier-session';
import { createDeliveryDispatchLocationStore, deliveryDispatchLocationOptions } from '../Configuration/location-store';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createCourierSessionModule(config: DeliveryDispatchServerConfig) {
  class CourierSessionModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-courier-session.log`)
            .traceLabel('courier-session');
          builder.addLocationStore(createDeliveryDispatchLocationStore(config));
          Object.assign(builder.configureLocations(), deliveryDispatchLocationOptions());
          return builder
            .addClientServerChannel(SampleNames.courierRouteChannel)
              .enableClient()
            .addStreamNode(SampleNames.courierStreamNode)
              .bind(config.courierStreamEndpoint)
              .registerSession(CourierSessionFactory)
            .build();
        }
      })
    ],
    providers: [
      CourierSessionFactory
    ]
  })(CourierSessionModule);

  return CourierSessionModule;
}

export { createCourierSessionModule };
