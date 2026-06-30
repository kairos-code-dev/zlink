import { Module } from '@nestjs/common';
import { ZLinkRegistryModule } from '@zlink-systems/nestjs';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createRegistryModule(config: DeliveryDispatchServerConfig) {
  class RegistryModule {}

  Module({
    imports: [
      ZLinkRegistryModule.forRoot({
        pubEndpoint: config.registryPubEndpoint,
        routerEndpoint: config.registryRouterEndpoint
      })
    ]
  })(RegistryModule);

  return RegistryModule;
}

export { createRegistryModule };
