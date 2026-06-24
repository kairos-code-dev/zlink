import { zlinkEntrySpotActorRequestHandler } from '@zlink-systems/nestjs';
import { BingoEntrySpot } from '../bingo-entry-spot';
import { PlayerActor } from '../../Actors/player-actor';
import { PacketNames } from '../../../../../../Shared/Contracts/messages';
import type {
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import type { BingoEntrySpot as BingoEntrySpotType } from '../bingo-entry-spot';
import type { PlayerActor as PlayerActorType } from '../../Actors/player-actor';
import type {
  MatchBingoReq,
  MatchBingoRes
} from '../../../../../../Shared/Contracts/messages';

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
    if (process.env.BINGO_DEBUG_FLOW === '1') {
      console.log(`play-match-handler start actor=${actor.actorId}`);
    }
    const actorRef = actor.context.actorRef;
    if (actorRef === undefined) {
      throw new Error(`Actor '${actor.actorId}' has no ActorRef.`);
    }
    return await entrySpot.matchActor(actorRef, {
      ...request,
      actorId: actor.actorId,
      displayName: actor.displayName
    });
  }
}

export { MatchBingoActorHandler };
