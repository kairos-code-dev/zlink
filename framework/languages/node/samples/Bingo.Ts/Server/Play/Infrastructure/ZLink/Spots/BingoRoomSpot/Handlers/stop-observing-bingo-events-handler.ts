import { Inject } from '@nestjs/common';
import {
  ZLINK_SPOT_HANDLE_RESOLVER,
  ZLINK_SPOT_OUTBOUND,
  zlinkSpotActorRequestHandler
} from '@zlink-systems/nestjs';
import { BingoRoomSpot } from '../bingo-room-spot';
import { PlayerActor } from '../../../Actors/player-actor';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import { StopObservingBingoEventsRes } from '../../../../../../../Shared/Contracts/bingo-messages.generated';
import { SampleNames } from '../../../../../../Configuration/sample-names';
import type {
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotHandleResolver,
  ZLinkSpotOutbound
} from '@zlink-systems/framework';
import type { StopObservingBingoEventsReq } from '../../../../../../../Shared/Contracts/messages';
import { VerifyStopObservingAtSpotReq } from './bingo-room-operation-handlers';

interface StopObservationDecision {
  readonly stopped: boolean;
  readonly observerNodeRid: string;
}

@zlinkSpotActorRequestHandler({
  actor: () => PlayerActor,
  spot: () => BingoRoomSpot,
  packetName: PacketNames.stopObservingBingoEventsReq
})
class StopObservingBingoEventsHandler
  implements ZLinkSpotActorRequestHandler<PlayerActor, StopObservingBingoEventsReq, StopObservingBingoEventsRes> {
  constructor(
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotHandles: ZLinkSpotHandleResolver,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly spotOutbound: ZLinkSpotOutbound
  ) {}

  async handle(
    actor: PlayerActor,
    _context: ZLinkSpotActorRequestContext,
    request: StopObservingBingoEventsReq
  ): Promise<StopObservingBingoEventsRes> {
    const spotRid = actor.context.spotRid;
    if (spotRid === undefined) throw new Error(`Bingo actor '${actor.actorId}' has no joined room.`);
    const spot = await this.spotHandles.resolveSpotHandle(SampleNames.roomSpotNode, spotRid);
    if (spot === undefined) throw new Error(`Bingo room '${String(spotRid)}' could not be resolved.`);
    const decision = await this.spotOutbound
      .requestToSpot(spot, new VerifyStopObservingAtSpotReq(actor.actorId, request.roomId))
      .yield<StopObservationDecision>();
    if (decision.stopped) await actor.context.leaveSpot();
    return new StopObservingBingoEventsRes(decision);
  }
}

export { StopObservingBingoEventsHandler };
