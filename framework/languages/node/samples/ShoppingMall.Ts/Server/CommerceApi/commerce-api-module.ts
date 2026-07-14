import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { createShoppingMallLocationStore, shoppingMallLocationOptions } from '../Configuration/location-store';
import { SHOPPINGMALL_SAMPLE_CONFIG, createShoppingMallConfigurationModule } from '../Configuration/sample-config';
import type { ShoppingMallServerConfig } from '../Configuration/sample-config';
import { OrderStore } from '../Shared/Store/order-store';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { OrderWorkflowRouterPort } from './Application/order-workflow-router-port';
import { StartOrderUseCase } from './Application/start-order-use-case';
import { ZLinkOrderWorkflowRouter } from './Infrastructure/ZLink/zlink-order-workflow-router';

function createShoppingMallCommerceApiModule(role: string): Function {
  class ShoppingMallCommerceApiModule {}
  const configuration = createShoppingMallConfigurationModule([
    role === SampleNames.apiA ? 'apiAHttpUrl' : 'apiBHttpUrl',
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
              .enableClient()
            .build();
        }
      })
    ],
    providers: [
      {
        provide: OrderStore,
        inject: [SHOPPINGMALL_SAMPLE_CONFIG],
        useFactory: (config: ShoppingMallServerConfig) => new OrderStore(config.workDir)
      },
      StartOrderUseCase,
      ZLinkOrderWorkflowRouter,
      { provide: OrderWorkflowRouterPort, useExisting: ZLinkOrderWorkflowRouter }
    ]
  })(ShoppingMallCommerceApiModule);
  return ShoppingMallCommerceApiModule;
}

export { createShoppingMallCommerceApiModule };
