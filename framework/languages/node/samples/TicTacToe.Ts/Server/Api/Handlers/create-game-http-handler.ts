const { Inject, Injectable } = require('@nestjs/common');
const { ZLINK_CHANNEL_CLIENT } = require('../../../../../../packages/nestjs/dist');
const {
  PacketNames,
  createGameHttpRes,
  createGameReq
} = require('../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../Configuration/sample-settings');
import type { ZLinkChannelClient } from '../../../../../packages/framework/dist';
import type {
  CreateGameHttpRes,
  CreateGameReq,
  CreateGameRes
} from '../../../Shared/Contracts/messages';

@Injectable()
class CreateGameEndpoint {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly playClient: ZLinkChannelClient) {}

  async handle(request: CreateGameReq): Promise<CreateGameHttpRes> {
    const response = await this.playClient
      .requestToChannel(SampleNames.playChannel, createGameReq(request.gameName))
      .packetName(PacketNames.createGame)
      .timeout(SampleTimings.requestTimeout)
      .submit<CreateGameRes>();
    return createGameHttpRes(response);
  }
}

export { CreateGameEndpoint };
