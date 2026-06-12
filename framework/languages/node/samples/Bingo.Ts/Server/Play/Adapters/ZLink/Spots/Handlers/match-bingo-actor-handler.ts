const { zlinkEntrySpotActorRequestHandler } = require('../../../../../../../../../packages/nestjs/dist');
const { BingoEntrySpot } = require('../bingo-entry-spot');
const { PlayerActor } = require('../../Actors/player-actor');
const { PacketNames } = require('../../../../../../Shared/Contracts/messages');
import type {
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '../../../../../../../../packages/framework/dist';
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
    return await entrySpot.matchActor(actor, request);
  }
}

export { MatchBingoActorHandler };
