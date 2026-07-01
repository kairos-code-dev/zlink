import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { EvidenceStore } from '../Configuration/evidence-store';
import { CustomerActorFactory } from './customer-actor';
import { CustomerEntrySpot } from './Spots/EntrySpot/customer-entry-spot';
import { DeliverySpotDirectory } from './Spots/DeliveryTrackingSpot/delivery-spot-directory';
import { DeliveryTrackingSpot } from './Spots/DeliveryTrackingSpot/delivery-tracking-spot';
import {
  DeliveryStatusChangedHandler,
  EnsureCustomerActorHandler,
  SubscribeCustomerToDeliveryHandler
} from './Handlers/tracking-handlers';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createTrackingModule(config: DeliveryDispatchServerConfig, evidence: EvidenceStore) {
  class TrackingModule {}
  const directory = new DeliverySpotDirectory();
  DeliveryTrackingSpot.useDirectory(directory);

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-tracking.log`)
            .traceLabel('tracking');
          return builder
            .useDiscovery()
              .addRegistryEndpoint(config.registryRouterEndpoint)
            .addClientServerChannel(SampleNames.trackingChannel)
              .enableServer(config.trackingEndpoint)
              .addHandlerGroup('tracking')
            .addFanoutChannel(SampleNames.statusFanoutChannel)
              .enablePublisher(config.statusFanoutEndpoint)
            .addSpotMesh(SampleNames.deliverySpotMesh)
              .enableRouter(config.trackingSpotRouterEndpoint, config.trackingSpotNodeRid)
              .enablePubSub(config.trackingSpotEndpoint, config.trackingSpotNodeRid)
              .addEntrySpot(CustomerEntrySpot)
            .addSpotFactory(DeliveryTrackingSpot)
            .actorFactory(SampleNames.customerActorType, CustomerActorFactory)
            .build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      CustomerActorFactory,
      CustomerEntrySpot,
      { provide: DeliverySpotDirectory, useValue: directory },
      DeliveryTrackingSpot,
      EnsureCustomerActorHandler,
      SubscribeCustomerToDeliveryHandler,
      DeliveryStatusChangedHandler
    ]
  })(TrackingModule);

  return TrackingModule;
}

export { createTrackingModule };
