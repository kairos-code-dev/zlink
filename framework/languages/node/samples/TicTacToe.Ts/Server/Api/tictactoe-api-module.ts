import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { AuthenticatePlayerHandler } from './Handlers/authenticate-player-handler';
import { CreateGameEndpoint } from './Handlers/create-game-http-handler';
import { PacketNames } from '../../Shared/Contracts/messages';
import { SampleNames } from '../Configuration/sample-settings';
import { TICTACTOE_SAMPLE_CONFIG, createTicTacToeConfigurationModule } from '../Configuration/sample-config';
import type { TicTacToeSampleConfig } from '../Configuration/sample-config';
function createTicTacToeApiModule() {
  class TicTacToeApiModule {}
  const configuration = createTicTacToeConfigurationModule([
    'apiHttpEndpoint', 'apiEndpoints', 'apiIndex', 'playSpotEndpoints', 'logDir'
  ]);

  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [TICTACTOE_SAMPLE_CONFIG],
        useFactory: (config: TicTacToeSampleConfig) => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${config.logDir}/flow-api-${config.apiIndex}.log`)
            .traceLabel(`api-${config.apiIndex}`);
          const mesh = builder.addRouteMesh(SampleNames.playSpotNode)
            .listen(config.apiEndpoints[config.apiIndex])
            .routingId(`api-${config.apiIndex + 1}`);
          mesh.channelName(SampleNames.apiChannel)
            .addRequestHandler(PacketNames.authenticatePlayerReq, AuthenticatePlayerHandler);
          mesh.channelName(SampleNames.playChannel).setWeight(0);
          mesh.channelName(SampleNames.playSpotNode).setWeight(0);
          for (const endpoint of config.playSpotEndpoints) {
            mesh.peerConnections().connect(endpoint);
          }
          return builder.build();
        }
      })
    ],
    providers: [
      CreateGameEndpoint,
      AuthenticatePlayerHandler
    ]
  })(TicTacToeApiModule);

  return TicTacToeApiModule;
}

function getCreateGameEndpoint(app: { get(token: unknown, options?: { strict?: boolean }): unknown }): InstanceType<typeof CreateGameEndpoint> {
  return app.get(CreateGameEndpoint, { strict: false }) as InstanceType<typeof CreateGameEndpoint>;
}

export { createTicTacToeApiModule, getCreateGameEndpoint };
