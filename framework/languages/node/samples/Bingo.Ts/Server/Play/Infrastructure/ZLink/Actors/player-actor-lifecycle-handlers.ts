import { Injectable } from '@nestjs/common';
import { ZLinkSpotActorSend } from '@zlink-systems/framework';
import type { ZLinkSpotActorSendContext } from '@zlink-systems/framework';
import type { PlayerActor } from './player-actor';
import { LeaveFinishedBingoRoom } from '../../../../../Shared/Contracts/bingo-messages.generated';

@Injectable()
class PendingBingoActorDestroyRegistry {
  private readonly actorIds = new Set<string>();

  mark(actorId: string): void {
    this.actorIds.add(actorId);
  }

  consume(actorId: string): boolean {
    return this.actorIds.delete(actorId);
  }
}

@Injectable()
class LeaveFinishedBingoRoomHandler {
  constructor(private readonly pendingDestroys: PendingBingoActorDestroyRegistry) {}

  @ZLinkSpotActorSend('LeaveFinishedBingoRoom')
  async handle(
    actor: PlayerActor,
    _context: ZLinkSpotActorSendContext,
    _message: LeaveFinishedBingoRoom
  ): Promise<void> {
    this.pendingDestroys.mark(actor.actorId);
    await actor.context.leaveSpot();
  }
}

export {
  LeaveFinishedBingoRoom,
  LeaveFinishedBingoRoomHandler,
  PendingBingoActorDestroyRegistry
};
