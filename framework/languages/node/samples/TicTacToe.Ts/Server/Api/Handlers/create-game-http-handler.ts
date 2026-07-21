import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { createGameHttpRes, createGameReq } from '../../../Shared/Contracts/messages';
import { SampleNames } from '../../Configuration/sample-settings';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type {
  CreateGameHttpRes,
  CreateGameHttpReq,
  CreateGameRes
} from '../../../Shared/Contracts/messages';

@Injectable()
class CreateGameEndpoint {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly playClient: ZLinkChannelClient) {}

  async handle(request: CreateGameHttpReq): Promise<CreateGameHttpRes> {
    const response = await this.playClient
      .requestToChannel(
        SampleNames.playSpotNode,
        SampleNames.playChannel,
        createGameReq(request.gameName ?? 'match')
      )
      .submit<CreateGameRes>();
    return createGameHttpRes(response);
  }
}

export { CreateGameEndpoint };
