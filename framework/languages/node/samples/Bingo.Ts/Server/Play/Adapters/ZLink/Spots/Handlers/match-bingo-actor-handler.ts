import type {
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '../../../../../../../../packages/framework/dist';
import type { BingoEntrySpot } from '../bingo-entry-spot';
import type { PlayerActor } from '../../Actors/player-actor';
import type {
  MatchBingoReq,
  MatchBingoRes
} from '../../../../../../Shared/Contracts/messages';

class MatchBingoActorHandler
  implements ZLinkEntrySpotActorRequestHandler<BingoEntrySpot, PlayerActor, MatchBingoReq, MatchBingoRes> {
  async handle(
    entrySpot: BingoEntrySpot,
    actor: PlayerActor,
    context: ZLinkSpotActorRequestContext,
    request: MatchBingoReq
  ): Promise<MatchBingoRes> {
    void context;
    return await entrySpot.matchActor(actor, request);
  }
}

export { MatchBingoActorHandler };
