import { zlinkSpotActorRequestHandler } from '@zlink-systems/nestjs';
import { BingoRoomSpot } from '../bingo-room-spot';
import { PlayerActor } from '../../../Actors/player-actor';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import { StopObservingBingoEventsRes } from '../../../../../../../Shared/Contracts/bingo-messages.generated';
import type {
  ZLinkSpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import type { BingoRoomSpot as BingoRoomSpotType } from '../bingo-room-spot';
import type { PlayerActor as PlayerActorType } from '../../../Actors/player-actor';
import type {
  StopObservingBingoEventsReq
} from '../../../../../../../Shared/Contracts/messages';

@zlinkSpotActorRequestHandler({
  actor: () => PlayerActor,
  spot: () => BingoRoomSpot,
  packetName: PacketNames.stopObservingBingoEventsReq
})
class StopObservingBingoEventsHandler
  implements ZLinkSpotActorRequestHandler<BingoRoomSpotType, PlayerActorType, StopObservingBingoEventsReq, StopObservingBingoEventsRes> {
  async handle(
    room: BingoRoomSpotType,
    actor: PlayerActorType,
    context: ZLinkSpotActorRequestContext,
    request: StopObservingBingoEventsReq
  ): Promise<StopObservingBingoEventsRes> {
    void context;
    const stopped = await room.stopObserving(actor, request);
    return new StopObservingBingoEventsRes({ stopped, observerNodeRid: String(room.context.nodeRid) });
  }
}

export { StopObservingBingoEventsHandler };
