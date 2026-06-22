import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Shared/Configuration/sample-names';

function createShoppingMallClientModule(config: { workflowEndpoint: string }) {
  class ShoppingMallClientModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .codecs()
            .addJson()
          .addClientServerChannel(SampleNames.workflowChannel)
            .enableClient(config.workflowEndpoint)
          .build()
      })
    ]
  })(ShoppingMallClientModule);

  return ShoppingMallClientModule;
}

export {
  createShoppingMallClientModule
};
