import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { createShoppingMallLocationStore, shoppingMallLocationOptions } from '../Configuration/location-store';
import type { ShoppingMallServerConfig } from '../Configuration/sample-config';
import { OrderStore } from '../Shared/Store/order-store';
import { orderWorkflowChannelFor, SampleNames } from '../../Shared/Configuration/sample-names';
import { OrderWorkflowService } from './Application/OrderWorkflow/order-workflow-service';
import { ContinueOrderWorkflowRouteHandler, PrepareInventoryReservedRouteHandler, RebuildOrderProjectionRouteHandler, StartOrderWorkflowRouteHandler } from './Infrastructure/ZLink/route-handlers';
import { ContinueWorkflowHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/continue-workflow-handler';
import { PrepareInventoryReservedHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/prepare-inventory-reserved-handler';
import { RebuildOrderProjectionHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/rebuild-order-projection-handler';
import { StartOrderWorkflowHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/start-order-workflow-handler';
import { OrderWorkflowSpot } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/order-workflow-spot';
import { SHOPPINGMALL_ROLE } from './order-workflow-tokens';

function createShoppingMallWorkflowModule(role: string, config: ShoppingMallServerConfig): Function {
  class ShoppingMallWorkflowModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.SHOPPINGMALL_LOG_DIR ?? 'logs'}/flow-${role}.log`)
            .traceLabel(role);
          builder.addLocationStore(createShoppingMallLocationStore(config));
          Object.assign(builder.configureLocations(), shoppingMallLocationOptions());
          return builder
            .addClientServerChannel(orderWorkflowChannelFor(role))
              .enableServer(workflowChannelEndpointForRole(role, config))
              .routingId(role)
              .addHandlerGroup('workflow')
            .addSpotMesh(SampleNames.orderWorkflowSpotMesh)
              .routingId(role)
              .enableRouter(workflowSpotEndpointForRole(role, config))
              .addSpotFactory(OrderWorkflowSpot)
            .build();
        }
      })
    ],
    providers: [
      { provide: SHOPPINGMALL_ROLE, useValue: role },
      { provide: OrderStore, useFactory: () => OrderStore.fromEnvironment() },
      OrderWorkflowService,
      StartOrderWorkflowRouteHandler,
      PrepareInventoryReservedRouteHandler,
      ContinueOrderWorkflowRouteHandler,
      RebuildOrderProjectionRouteHandler,
      StartOrderWorkflowHandler,
      PrepareInventoryReservedHandler,
      ContinueWorkflowHandler,
      RebuildOrderProjectionHandler
    ]
  })(ShoppingMallWorkflowModule);
  return ShoppingMallWorkflowModule;
}

function workflowChannelEndpointForRole(role: string, values: ShoppingMallServerConfig): string {
  if (role === SampleNames.workflowA) {
    return values.workflowAChannelEndpoint;
  }
  if (role === SampleNames.workflowB) {
    return values.workflowBChannelEndpoint;
  }
  throw new Error(`Role '${role}' is not an OrderWorkflow role.`);
}

function workflowSpotEndpointForRole(role: string, values: ShoppingMallServerConfig): string {
  if (role === SampleNames.workflowA) {
    return values.workflowASpotEndpoint;
  }
  if (role === SampleNames.workflowB) {
    return values.workflowBSpotEndpoint;
  }
  throw new Error(`Role '${role}' is not an OrderWorkflow role.`);
}

export { createShoppingMallWorkflowModule };
