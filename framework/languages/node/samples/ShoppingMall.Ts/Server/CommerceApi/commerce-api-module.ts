import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { createShoppingMallLocationStore, shoppingMallLocationOptions } from '../Configuration/location-store';
import type { ShoppingMallServerConfig } from '../Configuration/sample-config';
import { OrderStore } from '../Shared/Store/order-store';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { OrderWorkflowRouterPort } from './Application/order-workflow-router-port';
import { StartOrderUseCase } from './Application/start-order-use-case';
import { ZLinkOrderWorkflowRouter } from './Infrastructure/ZLink/zlink-order-workflow-router';

function createShoppingMallCommerceApiModule(role: string, config: ShoppingMallServerConfig): Function {
  class ShoppingMallCommerceApiModule {}

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
            .addClientServerChannel(SampleNames.orderWorkflowChannel)
              .enableClient()
            .build();
        }
      })
    ],
    providers: [
      { provide: OrderStore, useFactory: () => OrderStore.fromEnvironment() },
      StartOrderUseCase,
      ZLinkOrderWorkflowRouter,
      { provide: OrderWorkflowRouterPort, useExisting: ZLinkOrderWorkflowRouter }
    ]
  })(ShoppingMallCommerceApiModule);
  return ShoppingMallCommerceApiModule;
}

export { createShoppingMallCommerceApiModule };
