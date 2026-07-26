import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { AssignDeliveryHandler, OfferDeliveryResultHandler } from './Handlers/assign-delivery-handler';
import { DispatchWorker } from './dispatch-worker';
import { DeliveryOfferStore } from './delivery-offer-store';
import { createDeliveryDispatchLocationStore, deliveryDispatchLocationOptions } from '../Configuration/location-store';
import {
  DELIVERYDISPATCH_SAMPLE_CONFIG,
  createDeliveryDispatchConfigurationModule
} from '../Configuration/sample-config';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createDispatchCenterModule() {
  class DispatchCenterModule {}
  const configuration = createDeliveryDispatchConfigurationModule([
    'dispatchEndpoint',
    'dispatchSpotEndpoint',
    'redisEndpoint',
    'redisKeyPrefix',
    'logDir',
    'workDir'
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
            .traceLogFile(`${config.logDir}/flow-dispatch-center.log`)
            .traceLabel('dispatch-center');
          builder.addLocationStore(createDeliveryDispatchLocationStore(config));
          deliveryDispatchLocationOptions(builder.configureLocations());
          const mesh = builder.addRouteMesh(SampleNames.routeMesh)
            .listen(config.dispatchSpotEndpoint).useAllocatedRoutingId(16, 'delivery-dispatch');
          mesh.channelName(SampleNames.dispatchChannel).addHandlerGroup('dispatch');
          mesh.channelName(SampleNames.trackingChannel).setWeight(0);
          mesh.channelName(SampleNames.routeMesh);
          return builder.build();
        }
      })
    ],
    providers: [
      {
        provide: 'DELIVERYDISPATCH_LOCATION_STORE',
        inject: [DELIVERYDISPATCH_SAMPLE_CONFIG],
        useFactory: (config: DeliveryDispatchServerConfig) => createDeliveryDispatchLocationStore(config)
      },
      {
        provide: DeliveryOfferStore,
        inject: [DELIVERYDISPATCH_SAMPLE_CONFIG],
        useFactory: (config: DeliveryDispatchServerConfig) => new DeliveryOfferStore(config.workDir)
      },
      DispatchWorker,
      AssignDeliveryHandler,
      OfferDeliveryResultHandler
    ]
  })(DispatchCenterModule);

  return DispatchCenterModule;
}

export { createDispatchCenterModule };
