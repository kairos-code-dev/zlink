import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { createShoppingMallLocationStore, shoppingMallLocationOptions } from '../Configuration/location-store';
import { SHOPPINGMALL_SAMPLE_CONFIG, createShoppingMallConfigurationModule } from '../Configuration/sample-config';
import type { ShoppingMallServerConfig } from '../Configuration/sample-config';
import { OrderStore } from '../Shared/Store/order-store';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { OrderWorkflowService } from './Application/OrderWorkflow/order-workflow-service';
import { ContinueOrderWorkflowRouteHandler, PrepareInventoryEffectRouteHandler, PrepareInventoryReservedRouteHandler, RebuildOrderProjectionRouteHandler, StartOrderWorkflowRouteHandler, VerifyExpectedVersionFenceRouteHandler } from './Infrastructure/ZLink/route-handlers';
import { ContinueWorkflowHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/continue-workflow-handler';
import { PrepareInventoryReservedHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/prepare-inventory-reserved-handler';
import { PrepareInventoryEffectHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/prepare-inventory-effect-handler';
import { RebuildOrderProjectionHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/rebuild-order-projection-handler';
import { StartOrderWorkflowHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/start-order-workflow-handler';
import { OrderWorkflowSpot } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/order-workflow-spot';
import { ShoppingMallTopologyReadyHandler } from './Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/topology-ready-handler';
import { SHOPPINGMALL_ROLE } from './order-workflow-tokens';

function createShoppingMallWorkflowModule(role: string): Function {
  class ShoppingMallWorkflowModule {}
  const configuration = createShoppingMallConfigurationModule([
    role === SampleNames.workflowA ? 'workflowAHttpUrl' : 'workflowBHttpUrl',
    role === SampleNames.workflowA ? 'workflowAChannelEndpoint' : 'workflowBChannelEndpoint',
    role === SampleNames.workflowA ? 'workflowASpotEndpoint' : 'workflowBSpotEndpoint',
    role === SampleNames.workflowA ? 'workflowASpotPubEndpoint' : 'workflowBSpotPubEndpoint',
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
        inject: [SHOPPINGMALL_SAMPLE_CONFIG],
        useFactory: (config: ShoppingMallServerConfig) => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${config.logDir}/flow-${role}.log`)
            .traceLabel(role);
          builder.addLocationStore(createShoppingMallLocationStore(config));
          Object.assign(builder.configureLocations(), shoppingMallLocationOptions());
          return builder
            .addClientServerChannel(SampleNames.orderWorkflowChannel)
              .enableServer(workflowChannelEndpointForRole(role, config))
              .routingId(role)
              .addHandlerGroup('workflow')
            .addSpotMesh(SampleNames.orderWorkflowSpotMesh)
              .routingId(role)
              .enableRouter(workflowSpotEndpointForRole(role, config))
              .enablePubSub(workflowSpotPubEndpointForRole(role, config))
              .addSpotFactory(OrderWorkflowSpot)
            .build();
        }
      })
    ],
    providers: [
      { provide: SHOPPINGMALL_ROLE, useValue: role },
      {
        provide: OrderStore,
        inject: [SHOPPINGMALL_SAMPLE_CONFIG],
        useFactory: (config: ShoppingMallServerConfig) => new OrderStore(config.workDir)
      },
      OrderWorkflowService,
      StartOrderWorkflowRouteHandler,
      PrepareInventoryEffectRouteHandler,
      PrepareInventoryReservedRouteHandler,
      ContinueOrderWorkflowRouteHandler,
      RebuildOrderProjectionRouteHandler,
      VerifyExpectedVersionFenceRouteHandler,
      StartOrderWorkflowHandler,
      PrepareInventoryReservedHandler,
      PrepareInventoryEffectHandler,
      ContinueWorkflowHandler,
      RebuildOrderProjectionHandler,
      ShoppingMallTopologyReadyHandler
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

function workflowSpotPubEndpointForRole(role: string, values: ShoppingMallServerConfig): string {
  if (role === SampleNames.workflowA) return values.workflowASpotPubEndpoint;
  if (role === SampleNames.workflowB) return values.workflowBSpotPubEndpoint;
  throw new Error(`Role '${role}' is not an OrderWorkflow role.`);
}

export { createShoppingMallWorkflowModule };
