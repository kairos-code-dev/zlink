import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { AuthenticatePlayerHandler } from './Handlers/authenticate-player-handler';
import { CreateGameEndpoint } from './Handlers/create-game-http-handler';
import { PacketNames } from '../../Shared/Contracts/messages';
import { SampleNames } from '../Configuration/sample-settings';
function createTicTacToeApiModule(config: {
  apiEndpoint: string;
  playEndpoint: string;
}) {
  class TicTacToeApiModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .clientServerChannel(SampleNames.apiChannel, (channel) => channel
            .server(config.apiEndpoint)
            .requestHandler(PacketNames.authenticatePlayerReq, AuthenticatePlayerHandler))
          .clientServerChannel(SampleNames.playChannel, (channel) => channel
            .client(config.playEndpoint))
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
