import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { PacketNames } from '../../Shared/Contracts/messages';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { ContinueOrderWorkflowHandler } from './Handlers/continue-order-workflow-handler';
import {
  DeleteOrderProjectionHandler,
  GetOrderStateHandler,
  RebuildOrderProjectionHandler,
  SeedPendingIdempotencyHandler,
  ServerAssertionHandler
} from './Handlers/query-and-self-check-handlers';
import { StartOrderHandler } from './Handlers/start-order-handler';
import { OrderStore } from './order-store';

function createShoppingMallModule(config: {
  registryPubEndpoint: string;
  registryRouterEndpoint: string;
  workflowEndpoint: string;
}) {
  class ShoppingMallModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .codecs()
            .addJson()
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.workflowChannel)
            .enableServer(config.workflowEndpoint)
            .addRequestHandler(PacketNames.startOrderReq, StartOrderHandler)
            .addRequestHandler(PacketNames.continueOrderWorkflowReq, ContinueOrderWorkflowHandler)
            .addRequestHandler(PacketNames.getOrderStateReq, GetOrderStateHandler)
            .addRequestHandler(PacketNames.deleteOrderProjectionReq, DeleteOrderProjectionHandler)
            .addRequestHandler(PacketNames.rebuildOrderProjectionReq, RebuildOrderProjectionHandler)
            .addRequestHandler(PacketNames.seedPendingIdempotencyReq, SeedPendingIdempotencyHandler)
            .addRequestHandler(PacketNames.serverAssertionReq, ServerAssertionHandler)
          .build()
      })
    ],
    providers: [
      OrderStore,
      StartOrderHandler,
      ContinueOrderWorkflowHandler,
      GetOrderStateHandler,
      DeleteOrderProjectionHandler,
      RebuildOrderProjectionHandler,
      SeedPendingIdempotencyHandler,
      ServerAssertionHandler
    ]
  })(ShoppingMallModule);

  return ShoppingMallModule;
}

export {
  createShoppingMallModule
};
