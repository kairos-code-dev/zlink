import { Inject } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PacketNames, allocateBingoRoomReq, matchBingoApiRes } from '../../../Shared/Contracts/messages';
import { SampleNames } from '../../Configuration/sample-names';
import type {
  ZLinkChannelClient,
  ZLinkRequestHandler
} from '@zlink-systems/framework';
import type {
  AllocateBingoRoomRes,
  MatchBingoApiRes,
  MatchBingoReq
} from '../../../Shared/Contracts/messages';

@zlinkRequestHandler('api', PacketNames.matchBingoApiReq)
class MatchBingoHandler implements ZLinkRequestHandler<MatchBingoReq, MatchBingoApiRes> {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly zlinkClient: ZLinkChannelClient) {}

  async handle(request: MatchBingoReq): Promise<MatchBingoApiRes> {
    const allocated = await this.zlinkClient
      .requestToChannel(SampleNames.playChannel, allocateBingoRoomReq(request.mode))
      .submit<AllocateBingoRoomRes>();
    return matchBingoApiRes(allocated.roomId);
  }
}

export { MatchBingoHandler };
