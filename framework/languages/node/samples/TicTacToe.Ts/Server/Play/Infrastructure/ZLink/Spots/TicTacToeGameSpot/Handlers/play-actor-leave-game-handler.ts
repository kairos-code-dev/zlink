import { Inject, Injectable } from '@nestjs/common';
import {
  ZLINK_SPOT_HANDLE_RESOLVER,
  ZLINK_SPOT_OUTBOUND
} from '@zlink-systems/nestjs';
import { ZLinkSpotActorSend } from '@zlink-systems/framework';
import { SampleNames } from '../../../../../../Configuration/sample-settings';
import type {
  ZLinkSpotActorSendContext,
  ZLinkSpotHandleResolver,
  ZLinkSpotOutbound
} from '@zlink-systems/framework';
import type { PlayActor } from '../../../Actors/play-actor';
import type { LeaveGameReq } from '../../../../../../../Shared/Contracts/messages';
import { PendingActorDestroyRegistry } from '../../EntrySpot/play-entry-spot';
import { VerifyLeaveGameAtSpotReq } from './tictactoe-game-operation-handlers';

@Injectable()
class PlayActorLeaveGameHandler {
  constructor(
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotHandles: ZLinkSpotHandleResolver,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly spotOutbound: ZLinkSpotOutbound,
    private readonly pendingDestroys: PendingActorDestroyRegistry
  ) {}

  @ZLinkSpotActorSend('LeaveGameReq')
  async handle(
    actor: PlayActor,
    _context: ZLinkSpotActorSendContext,
    request: LeaveGameReq
  ): Promise<void> {
    const spotRid = actor.context.spotRid;
    if (spotRid === undefined || String(spotRid) !== request.roomId) {
      throw new Error(`Actor requested leave for a different room. roomId=${request.roomId}`);
    }
    const spot = await this.spotHandles.resolveSpotHandle(SampleNames.playSpotNode, spotRid);
    if (spot === undefined) {
      throw new Error(`Game spot '${String(spotRid)}' could not be resolved.`);
    }
    await this.spotOutbound
      .requestToSpot(spot, new VerifyLeaveGameAtSpotReq(actor.actorId, request.roomId))
      .yield<{ readonly allowed: true }>();
    this.pendingDestroys.mark(actor.actorId);
    await actor.context.leaveSpot();
    actor.roomId = undefined;
    console.log(`actor: LeaveGameReq completed. actor=${actor.actorId}`);
  }
}

export { PlayActorLeaveGameHandler };
