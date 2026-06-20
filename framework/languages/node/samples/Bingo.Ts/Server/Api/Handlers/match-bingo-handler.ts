import { Inject } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PacketNames, allocateBingoRoomReq, matchBingoApiRes, withPlayerIdentity } from '../../../Shared/Contracts/messages';
import { SampleNames } from '../../Configuration/sample-names';
import type {
  ZLinkChannelClient,
  ZLinkRequestHandler
} from '@zlink-systems/framework';
import type {
  AllocateBingoRoomRes,
  MatchBingoApiRes,
  MatchBingoReq,
  PlayerIdentity
} from '../../../Shared/Contracts/messages';

@zlinkRequestHandler('api', PacketNames.matchBingoApiReq)
class MatchBingoHandler implements ZLinkRequestHandler<MatchBingoReq & PlayerIdentity, MatchBingoApiRes> {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly zlinkClient: ZLinkChannelClient) {}

  async handle(request: MatchBingoReq & PlayerIdentity): Promise<MatchBingoApiRes> {
    const allocated = await this.zlinkClient
      .requestToChannel(
        SampleNames.playChannel,
        withPlayerIdentity(allocateBingoRoomReq(request.mode), request.actorId, request.displayName)
      )
      .submit<AllocateBingoRoomRes>();
    return matchBingoApiRes(allocated.roomId, allocated.roomOwnerNodeRid);
  }
}

export { MatchBingoHandler };
