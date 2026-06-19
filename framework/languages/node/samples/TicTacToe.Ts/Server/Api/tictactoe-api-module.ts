import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { AuthenticatePlayerHandler } from './Handlers/authenticate-player-handler';
import { CreateGameEndpoint } from './Handlers/create-game-http-handler';
import { PacketNames } from '../../Shared/Contracts/messages';
import { SampleNames, SampleTimings } from '../Configuration/sample-settings';
function createTicTacToeApiModule(config: {
  apiEndpoints: string[];
  apiIndex: number;
  playChannelEndpoints: string[];
}) {
  class TicTacToeApiModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .options({ requestTimeoutMs: SampleTimings.requestTimeout })
          .codecs()
            .addJson()
          .addClientServerChannel(SampleNames.apiChannel)
            .enableServer(config.apiEndpoints[config.apiIndex])
            .addRequestHandler(PacketNames.authenticatePlayerReq, AuthenticatePlayerHandler)
          .addClientServerChannel(SampleNames.playChannel)
            .enableClient(config.playChannelEndpoints)
          .build()
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
