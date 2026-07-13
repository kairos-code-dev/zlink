import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { CourierActorDirectory, CourierActorFactory } from './courier-actor';
import { CourierEntrySpot } from './courier-entry-spot';
import { CourierActorBindHandler, CourierActorDecisionHandler, CourierActorOfferHandler, CourierActorSessionBindHandler, EnsureCourierActorHandler, OfferDeliveryActorNodeHandler } from './offer-delivery-handler';
import { createDeliveryDispatchLocationStore, deliveryDispatchLocationOptions } from '../Configuration/location-store';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

type CourierOptions = {
  courierId: string;
};

function createCourierActorNodeModule(config: DeliveryDispatchServerConfig, options: CourierOptions) {
  class CourierActorNodeModule {}
  const directory = new CourierActorDirectory();
  CourierActorFactory.useDirectory(directory);
  const endpoint = options.courierId === 'courier-a'
    ? config.courierActorNode1RouteEndpoint
    : config.courierActorNode2RouteEndpoint;
  const spotEndpoint = options.courierId === 'courier-a'
    ? config.courierActorNode1SpotEndpoint
    : config.courierActorNode2SpotEndpoint;
  const nodeRid = options.courierId === 'courier-a' ? 'courier-node-1' : 'courier-node-2';

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-${nodeRid}.log`)
            .traceLabel(nodeRid);
          builder.addLocationStore(createDeliveryDispatchLocationStore(config));
          Object.assign(builder.configureLocations(), deliveryDispatchLocationOptions());
          return builder
            .addRouteMeshChannel(SampleNames.courierActorNodeRouteChannel)
              .enableRouter(endpoint)
              .routingId(nodeRid)
              .addHandlerGroup('courier-actor-node')
            .addSpotMesh(SampleNames.courierActorSpotMesh)
              .enableRouter(spotEndpoint, nodeRid)
              .addEntrySpot(CourierEntrySpot)
              .actorFactory(SampleNames.courierActorType, CourierActorFactory)
            .build();
        }
      })
    ],
    providers: [
      { provide: CourierActorDirectory, useValue: directory },
      CourierActorFactory,
      CourierEntrySpot,
      OfferDeliveryActorNodeHandler,
      EnsureCourierActorHandler,
      CourierActorBindHandler,
      CourierActorSessionBindHandler,
      CourierActorOfferHandler,
      CourierActorDecisionHandler
    ]
  })(CourierActorNodeModule);

  return CourierActorNodeModule;
}

export {
  createCourierActorNodeModule
};

export type {
  CourierOptions
};
