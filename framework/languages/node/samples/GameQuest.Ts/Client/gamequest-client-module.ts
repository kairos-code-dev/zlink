import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Shared/Configuration/sample-names';

function createGameQuestClientModule(config: { questEndpoint: string }) {
  class GameQuestClientModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .codecs()
            .addJson()
          .addClientServerChannel(SampleNames.questChannel)
            .enableClient(config.questEndpoint)
          .build()
      })
    ]
  })(GameQuestClientModule);

  return GameQuestClientModule;
}

export {
  createGameQuestClientModule
};
