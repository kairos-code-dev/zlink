import { Inject } from '@nestjs/common';
import { ZLINK_ROUTE_CLIENT, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../Shared/Contracts/messages';
import { AllocateBingoRoomReq, MatchBingoApiRes } from '../../../Shared/Contracts/bingo-messages.generated';
import { SampleNames } from '../../Configuration/sample-names';
import type {
  ZLinkRouteClient,
  ZLinkRequestHandler
} from '@zlink-systems/framework';
import type {
  AllocateBingoRoomRes,
  MatchBingoApiReq
} from '../../../Shared/Contracts/messages';

@zlinkRequestHandler('api', PacketNames.matchBingoApiReq)
class MatchBingoHandler implements ZLinkRequestHandler<MatchBingoApiReq, MatchBingoApiRes> {
  constructor(@Inject(ZLINK_ROUTE_CLIENT) private readonly routes: ZLinkRouteClient) {}

  async handle(request: MatchBingoApiReq): Promise<MatchBingoApiRes> {
    const allocated = await this.routes
      .requestToNode(
        SampleNames.playChannel,
        request.actorNodeRid,
        new AllocateBingoRoomReq({
          mode: request.mode,
          actorId: request.actorId,
          preferredOwnerNodeRid: request.actorNodeRid
        })
      )
      .submit<AllocateBingoRoomRes>();
    return new MatchBingoApiRes({ roomId: allocated.roomId, roomOwnerNodeRid: allocated.roomOwnerNodeRid });
  }
}

export { MatchBingoHandler };
