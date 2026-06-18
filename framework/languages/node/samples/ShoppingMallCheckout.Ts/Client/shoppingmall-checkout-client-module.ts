import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames, SampleTimings } from '../Shared/Configuration/sample-names';

function createShoppingMallCheckoutClientModule(config: { checkoutEndpoint: string }) {
  class ShoppingMallCheckoutClientModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .options({ requestTimeoutMs: SampleTimings.requestTimeout })
          .codecs()
            .addJson()
          .addClientServerChannel(SampleNames.checkoutChannel)
            .enableClient(config.checkoutEndpoint)
          .build()
      })
    ]
  })(ShoppingMallCheckoutClientModule);

  return ShoppingMallCheckoutClientModule;
}

export {
  createShoppingMallCheckoutClientModule
};
