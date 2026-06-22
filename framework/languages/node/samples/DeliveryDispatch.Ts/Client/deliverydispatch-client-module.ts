import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Shared/Configuration/sample-names';

function createDeliveryDispatchClientModule(config: { registryRouterEndpoint: string; dispatchEndpoint: string }) {
  class DeliveryDispatchClientModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .codecs()
            .addJson()
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.dispatchChannel)
            .enableClient()
          .build()
      })
    ]
  })(DeliveryDispatchClientModule);

  return DeliveryDispatchClientModule;
}

export {
  createDeliveryDispatchClientModule
};
