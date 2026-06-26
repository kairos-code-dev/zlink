import { zlinkEntrySpotActorRequestHandler } from '@zlink-systems/nestjs';
import { BingoEntrySpot } from '../bingo-entry-spot';
import { PlayerActor } from '../../../Actors/player-actor';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import type {
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import type { BingoEntrySpot as BingoEntrySpotType } from '../bingo-entry-spot';
import type { PlayerActor as PlayerActorType } from '../../../Actors/player-actor';
import type {
  ObserveBingoEventsReq,
  ObserveBingoEventsRes
} from '../../../../../../../Shared/Contracts/messages';

@zlinkEntrySpotActorRequestHandler({
  actor: () => PlayerActor,
  entrySpot: () => BingoEntrySpot,
  packetName: PacketNames.observeBingoEventsReq
})
class ObserveBingoEventsHandler
  implements ZLinkEntrySpotActorRequestHandler<BingoEntrySpotType, PlayerActorType, ObserveBingoEventsReq, ObserveBingoEventsRes> {
  async handle(
    entrySpot: BingoEntrySpotType,
    actor: PlayerActorType,
    context: ZLinkSpotActorRequestContext,
    request: ObserveBingoEventsReq
  ): Promise<ObserveBingoEventsRes> {
    void context;
    return await entrySpot.observeEvents(actor, request);
  }
}

export { ObserveBingoEventsHandler };
