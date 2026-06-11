const { Inject } = require('@nestjs/common');
const { ZLINK_CHANNEL_CLIENT } = require('../../../../../../packages/nestjs/dist');
const {
  PacketNames,
  allocateBingoRoomReq,
  matchBingoApiRes
} = require('../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../Configuration/sample-names');
import type {
  AllocateBingoRoomRes,
  MatchBingoApiRes,
  MatchBingoReq
} from '../../../Shared/Contracts/messages';

class MatchBingoHandler {
  [key: string]: any;

  constructor(@Inject(ZLINK_CHANNEL_CLIENT) zlinkClient: any) {
    this.zlinkClient = zlinkClient;
  }

  async handle(request: MatchBingoReq): Promise<MatchBingoApiRes> {
    const allocated: AllocateBingoRoomRes = await this.zlinkClient
      .requestToChannel(SampleNames.playChannel, allocateBingoRoomReq(request.mode))
      .packetName(PacketNames.allocateBingoRoom)
      .timeout(SampleTimings.requestTimeout)
      .submit();
    return matchBingoApiRes(allocated.roomId);
  }
}

export { MatchBingoHandler };
