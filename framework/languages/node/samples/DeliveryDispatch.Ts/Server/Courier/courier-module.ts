import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { CourierActorDirectory, CourierActorFactory } from './courier-actor';
import { CourierEntrySpot } from './courier-entry-spot';
import { CourierActorBindHandler, CourierActorDecisionHandler, CourierActorOfferHandler, CourierActorSessionBindHandler, EnsureCourierActorHandler, OfferDeliveryEntrySpotHandler } from './offer-delivery-handler';
import { createDeliveryDispatchLocationStore, deliveryDispatchLocationOptions } from '../Configuration/location-store';
import {
  DELIVERYDISPATCH_SAMPLE_CONFIG,
  createDeliveryDispatchConfigurationModule
} from '../Configuration/sample-config';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

type CourierOptions = {
  courierId: string;
};

function createCourierActorNodeModule(options: CourierOptions) {
  class CourierActorNodeModule {}
  const directory = new CourierActorDirectory();
  const nodeRid = options.courierId === 'courier-a' ? 'courier-node-1' : 'courier-node-2';
  const spotEndpointKey = options.courierId === 'courier-a'
    ? 'courierActorNode1SpotEndpoint'
    : 'courierActorNode2SpotEndpoint';
  const configuration = createDeliveryDispatchConfigurationModule([
    spotEndpointKey,
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
          const spotEndpoint = config[spotEndpointKey];
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${config.logDir}/flow-${nodeRid}.log`)
            .traceLabel(nodeRid);
          builder.addLocationStore(createDeliveryDispatchLocationStore(config));
          Object.assign(builder.configureLocations(), deliveryDispatchLocationOptions());
          return builder
            .addClientServerChannel(SampleNames.dispatchChannel)
              .enableClient()
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
      OfferDeliveryEntrySpotHandler,
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
