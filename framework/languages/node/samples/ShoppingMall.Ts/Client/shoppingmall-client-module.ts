import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Shared/Configuration/sample-names';

function createShoppingMallClientModule(config: { registryRouterEndpoint: string; workflowEndpoint: string }) {
  class ShoppingMallClientModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .codecs()
            .addJson()
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.workflowChannel)
            .enableClient()
          .build()
      })
    ]
  })(ShoppingMallClientModule);

  return ShoppingMallClientModule;
}

export {
  createShoppingMallClientModule
};
