import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Shared/Configuration/sample-names';

function createGameQuestClientModule(config: { registryRouterEndpoint: string; questEndpoint: string }) {
  class GameQuestClientModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .codecs()
            .addJson()
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.questChannel)
            .enableClient()
          .build()
      })
    ]
  })(GameQuestClientModule);

  return GameQuestClientModule;
}

export {
  createGameQuestClientModule
};
