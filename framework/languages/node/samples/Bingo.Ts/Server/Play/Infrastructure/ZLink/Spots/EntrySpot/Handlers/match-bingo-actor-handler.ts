import { zlinkEntrySpotActorRequestHandler } from '@zlink-systems/nestjs';
import { BingoEntrySpot } from '../bingo-entry-spot';
import { PlayerActor } from '../../../Actors/player-actor';
import {
  bingoRoomJoinReq,
  matchBingoApiReq,
  matchBingoRes,
  PacketNames
} from '../../../../../../../Shared/Contracts/messages';
import { SampleNames, SampleTimings } from '../../../../../../Configuration/sample-names';
import type {
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import type { BingoEntrySpot as BingoEntrySpotType } from '../bingo-entry-spot';
import type { PlayerActor as PlayerActorType } from '../../../Actors/player-actor';
import type {
  BingoRoomJoinRes,
  MatchBingoApiRes,
  MatchBingoReq,
  MatchBingoRes
} from '../../../../../../../Shared/Contracts/messages';

@zlinkEntrySpotActorRequestHandler({
  actor: () => PlayerActor,
  entrySpot: () => BingoEntrySpot,
  packetName: PacketNames.matchBingoReq
})
class MatchBingoActorHandler
  implements ZLinkEntrySpotActorRequestHandler<BingoEntrySpotType, PlayerActorType, MatchBingoReq, MatchBingoRes> {
  async handle(
    entrySpot: BingoEntrySpotType,
    actor: PlayerActorType,
    context: ZLinkSpotActorRequestContext,
    request: MatchBingoReq
  ): Promise<MatchBingoRes> {
    void context;
    const actorId = actor.actorId;
    const displayName = actor.displayName;
    const matched = await entrySpot.context.outbound
        .requestToChannel(
          SampleNames.apiChannel,
          matchBingoApiReq(actorId, displayName, request.mode, String(entrySpot.context.nodeRid))
        )
        .packetName(PacketNames.matchBingoApiReq)
        .timeout(SampleTimings.requestTimeout)
        .submit<MatchBingoApiRes>();

    const roomId = matched.roomId;
    const joined = await actor.context
      .joinSpot(roomId, bingoRoomJoinReq(roomId, actorId, displayName))
      .timeout(SampleTimings.requestTimeout)
      .submit<BingoRoomJoinRes>();
    if (!joined.accepted || joined.reply === undefined) {
      throw new Error(`Room ${roomId} rejected actor '${actorId}'.`);
    }

    return matchBingoRes(roomId, joined.reply.state, matched.roomOwnerNodeRid);
  }
}

export { MatchBingoActorHandler };
