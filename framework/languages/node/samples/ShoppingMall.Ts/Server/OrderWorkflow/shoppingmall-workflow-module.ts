import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
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
import { OrderWorkflowService } from './Application/OrderWorkflow/order-workflow-service';
import { OrderWorkflowSpot } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/order-workflow-spot';
import { ContinueOrderWorkflowSpotHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/continue-order-workflow-handler';
import { RebuildOrderProjectionSpotHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/rebuild-order-projection-handler';
import { StartOrderWorkflowSpotHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/start-order-workflow-handler';
import { OrderStore } from '../Shared/Store/order-store';

function createShoppingMallModule(config: {
  registryPubEndpoint: string;
  registryRouterEndpoint: string;
}, workflowEndpoint: string, instanceId: string) {
  class ShoppingMallModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.SHOPPINGMALL_LOG_DIR ?? 'logs'}/flow-${instanceId}.log`)
            .traceLabel(instanceId);
          return builder
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addRouteMesh(SampleNames.orderWorkflowRouteChannel)
            .enableRouter(workflowEndpoint)
            .routingId(instanceId)
            .addRequestHandler(PacketNames.startOrderWorkflowReq, StartOrderHandler)
            .addRequestHandler(PacketNames.continueOrderWorkflowReq, ContinueOrderWorkflowHandler)
            .addRequestHandler(PacketNames.getOrderStateReq, GetOrderStateHandler)
            .addRequestHandler(PacketNames.deleteOrderProjectionReq, DeleteOrderProjectionHandler)
            .addRequestHandler(PacketNames.rebuildOrderProjectionReq, RebuildOrderProjectionHandler)
            .addRequestHandler(PacketNames.seedPendingIdempotencyReq, SeedPendingIdempotencyHandler)
            .addRequestHandler(PacketNames.serverAssertionReq, ServerAssertionHandler)
          .addSpotMesh(SampleNames.orderWorkflowRouteChannel)
            .addSpotFactory(OrderWorkflowSpot)
          .build();
        }
      })
    ],
    providers: [
      OrderStore,
      OrderWorkflowService,
      OrderWorkflowSpot,
      StartOrderHandler,
      ContinueOrderWorkflowHandler,
      StartOrderWorkflowSpotHandler,
      ContinueOrderWorkflowSpotHandler,
      RebuildOrderProjectionSpotHandler,
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
