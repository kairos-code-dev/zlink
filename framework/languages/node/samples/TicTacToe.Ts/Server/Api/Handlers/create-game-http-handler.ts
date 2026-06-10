const { Inject } = require('@nestjs/common');
const { ZLINK_CHANNEL_CLIENT } = require('../../../../../../packages/nestjs/dist');
const {
  PacketNames,
  createGameHttpRes,
  createGameReq
} = require('../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../../Shared/Configuration/sample-settings');
import type {
  CreateGameHttpRes,
  CreateGameReq,
  CreateGameRes
} from '../../../Shared/Contracts/messages';

class CreateGameHttpHandler {
  [key: string]: any;
  constructor(playClient: any) {
    this.playClient = playClient;
  }

  async handle(request: CreateGameReq): Promise<CreateGameHttpRes> {
    const response: CreateGameRes = await this.playClient
      .requestToChannel(SampleNames.playChannel, createGameReq(request.gameName))
      .packetName(PacketNames.createGame)
      .timeout(SampleTimings.requestTimeout)
      .submit();
    return createGameHttpRes(response);
  }
}

Inject(ZLINK_CHANNEL_CLIENT)(CreateGameHttpHandler, undefined, 0);

export { CreateGameHttpHandler };
