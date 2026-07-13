import { zlinkEntrySpotActorRequestHandler } from '@zlink-systems/nestjs';
import { BingoEntrySpot } from '../bingo-entry-spot';
import { PlayerActor } from '../../../Actors/player-actor';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import {
  BingoRoomJoinReq,
  MatchBingoApiReq,
  MatchBingoRes
} from '../../../../../../../Shared/Contracts/bingo-messages.generated';
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
  MatchBingoReq
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
          new MatchBingoApiReq({
            actorId,
            displayName,
            actorNodeRid: String(entrySpot.context.nodeRid),
            mode: request.mode
          })
        )
        .timeout(SampleTimings.requestTimeout)
        .submit<MatchBingoApiRes>();

    const roomId = matched.roomId;
    const joined = await actor.context
      .joinSpot(roomId, new BingoRoomJoinReq({ roomId, actorId, displayName, observeOnly: false }))
      .timeout(SampleTimings.requestTimeout)
      .submit<BingoRoomJoinRes>();
    if (joined.status === 'rejected') {
      throw new Error(`Room ${roomId} rejected actor '${actorId}'.`);
    }

    return new MatchBingoRes({ roomId, state: joined.reply.state, roomOwnerNodeRid: matched.roomOwnerNodeRid });
  }
}

export { MatchBingoActorHandler };
