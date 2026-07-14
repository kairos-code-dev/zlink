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
    'apiHttpEndpoint', 'apiEndpoints', 'apiIndex', 'playChannelEndpoints', 'logDir'
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
          return builder
          .addClientServerChannel(SampleNames.apiChannel)
            .enableServer(config.apiEndpoints[config.apiIndex])
            .addRequestHandler(PacketNames.authenticatePlayerReq, AuthenticatePlayerHandler)
          .addClientServerChannel(SampleNames.playChannel)
            .enableClient(config.playChannelEndpoints)
          .build();
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
