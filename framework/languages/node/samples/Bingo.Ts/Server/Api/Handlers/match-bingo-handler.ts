import { Inject } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../Shared/Contracts/messages';
import { AllocateBingoRoomReq, MatchBingoApiRes } from '../../../Shared/Contracts/bingo-messages.generated';
import { SampleNames } from '../../Configuration/sample-names';
import type {
  ZLinkChannelClient,
  ZLinkRequestHandler
} from '@zlink-systems/framework';
import type {
  AllocateBingoRoomRes,
  MatchBingoApiReq
} from '../../../Shared/Contracts/messages';

@zlinkRequestHandler('api', PacketNames.matchBingoApiReq)
class MatchBingoHandler implements ZLinkRequestHandler<MatchBingoApiReq, MatchBingoApiRes> {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient) {}

  async handle(request: MatchBingoApiReq): Promise<MatchBingoApiRes> {
    const allocated = await this.channels
      .requestToChannel(
        SampleNames.roomSpotNode,
        SampleNames.playChannel,
        new AllocateBingoRoomReq({
          mode: request.mode,
          actorId: request.actorId
        })
      )
      .submit<AllocateBingoRoomRes>();
    return new MatchBingoApiRes({ roomId: allocated.roomId });
  }
}

export { MatchBingoHandler };
