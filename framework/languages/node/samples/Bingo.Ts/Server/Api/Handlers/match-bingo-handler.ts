const { Inject } = require('@nestjs/common');
const { ZLINK_CHANNEL_CLIENT } = require('../../../../../../packages/nestjs/dist');
const {
  PacketNames,
  allocateBingoRoomReq,
  matchBingoApiRes
} = require('../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../Configuration/sample-names');
import type {
  ZLinkChannelClient,
  ZLinkRequestHandler
} from '../../../../../packages/framework/dist';
import type {
  AllocateBingoRoomRes,
  MatchBingoApiRes,
  MatchBingoReq
} from '../../../Shared/Contracts/messages';

class MatchBingoHandler implements ZLinkRequestHandler<MatchBingoReq, MatchBingoApiRes> {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly zlinkClient: ZLinkChannelClient) {}

  async handle(request: MatchBingoReq): Promise<MatchBingoApiRes> {
    const allocated = await this.zlinkClient
      .requestToChannel(SampleNames.playChannel, allocateBingoRoomReq(request.mode))
      .packetName(PacketNames.allocateBingoRoom)
      .timeout(SampleTimings.requestTimeout)
      .submit<AllocateBingoRoomRes>();
    return matchBingoApiRes(allocated.roomId);
  }
}

export { MatchBingoHandler };
