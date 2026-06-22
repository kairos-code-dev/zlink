import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { PacketNames } from '../../Shared/Contracts/messages';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { AdvanceDeliveryHandler } from './Handlers/advance-delivery-handler';
import { AssignDeliveryHandler } from './Handlers/assign-delivery-handler';
import { CreateDeliveryHandler } from './Handlers/create-delivery-handler';
import { ServerAssertionHandler } from './Handlers/server-assertion-handler';
import { SubscribeDeliveryHandler } from './Handlers/subscribe-delivery-handler';
import { DeliveryStore } from './delivery-store';

function createDeliveryDispatchModule(config: {
  registryPubEndpoint: string;
  registryRouterEndpoint: string;
  dispatchEndpoint: string;
}) {
  class DeliveryDispatchModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .codecs()
            .addJson()
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.dispatchChannel)
            .enableServer(config.dispatchEndpoint)
            .addRequestHandler(PacketNames.createDeliveryReq, CreateDeliveryHandler)
            .addRequestHandler(PacketNames.subscribeDeliveryReq, SubscribeDeliveryHandler)
            .addRequestHandler(PacketNames.assignDeliveryReq, AssignDeliveryHandler)
            .addRequestHandler(PacketNames.advanceDeliveryReq, AdvanceDeliveryHandler)
            .addRequestHandler(PacketNames.serverAssertionReq, ServerAssertionHandler)
          .build()
      })
    ],
    providers: [
      DeliveryStore,
      CreateDeliveryHandler,
      SubscribeDeliveryHandler,
      AssignDeliveryHandler,
      AdvanceDeliveryHandler,
      ServerAssertionHandler
    ]
  })(DeliveryDispatchModule);

  return DeliveryDispatchModule;
}

export {
  createDeliveryDispatchModule
};
