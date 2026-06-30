import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { GameQuestSessionFactory } from './gamequest-session';
import { PlayerSessionDirectory } from './player-session-directory';
import type { GameQuestServerConfig } from '../Configuration/sample-config';

function createGameApiModule(config: GameQuestServerConfig, instanceId: 'api-a' | 'api-b') {
  class GameApiModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.GAMEQUEST_LOG_DIR ?? 'logs'}/flow-${instanceId}.log`)
            .traceLabel(instanceId);
          return builder
            .useDiscovery()
              .addRegistryEndpoint(config.registryRouterEndpoint)
            .addRouteMesh(SampleNames.questMissionRouteChannel)
              .routingId(`${instanceId}-route`)
              .connect([config.missionAEndpoint, config.missionBEndpoint])
            .addStreamNode(SampleNames.playerStreamNode)
              .bind(instanceId === 'api-a' ? config.apiAStreamEndpoint : config.apiBStreamEndpoint)
              .registerSession(GameQuestSessionFactory)
            .build();
        }
      })
    ],
    providers: [
      PlayerSessionDirectory,
      GameQuestSessionFactory
    ]
  })(GameApiModule);

  return GameApiModule;
}

export {
  createGameApiModule
};
