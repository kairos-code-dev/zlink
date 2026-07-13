import { Inject } from '@nestjs/common';
import { ZLINK_ROUTE_CLIENT, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PacketNames, allocateBingoRoomReq, matchBingoApiRes, withPlayerIdentity } from '../../../Shared/Contracts/messages';
import { SampleNames } from '../../Configuration/sample-names';
import type {
  ZLinkRouteClient,
  ZLinkRequestHandler
} from '@zlink-systems/framework';
import type {
  AllocateBingoRoomRes,
  MatchBingoApiReq,
  MatchBingoApiRes
} from '../../../Shared/Contracts/messages';

@zlinkRequestHandler('api', PacketNames.matchBingoApiReq)
class MatchBingoHandler implements ZLinkRequestHandler<MatchBingoApiReq, MatchBingoApiRes> {
  constructor(@Inject(ZLINK_ROUTE_CLIENT) private readonly routes: ZLinkRouteClient) {}

  async handle(request: MatchBingoApiReq): Promise<MatchBingoApiRes> {
    const allocated = await this.routes
      .requestToNode(
        SampleNames.playChannel,
        request.actorNodeRid,
        withPlayerIdentity(allocateBingoRoomReq(request.mode), request.actorId, request.displayName)
      )
      .submit<AllocateBingoRoomRes>();
    return matchBingoApiRes(allocated.roomId, allocated.roomOwnerNodeRid);
  }
}

export { MatchBingoHandler };
