import { Module } from '@nestjs/common';
import { ZLinkRegistryModule } from '@zlink-systems/nestjs';
import type { GameQuestServerConfig } from '../Configuration/sample-config';

function createRegistryModule(config: GameQuestServerConfig) {
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

export {
  createRegistryModule
};
