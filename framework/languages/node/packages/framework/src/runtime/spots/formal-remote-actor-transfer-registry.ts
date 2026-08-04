import type { RoutingId, ZLinkActor } from '../../contracts';
import type { ZLinkDeferredJoinAcceptedRoot } from '../actors';
import type { ZLinkActorHandoffPacket } from '../actors/actor-handoff';

export interface ZLinkFormalRemoteActorTransfer {
  readonly actor: ZLinkActor;
  readonly spotId: RoutingId;
  readonly transferId: string;
  readonly handoffBacklog: readonly ZLinkActorHandoffPacket[];
  readonly deferredJoinRoot?: ZLinkDeferredJoinAcceptedRoot;
  readonly sourceLeaveTerminal: Promise<boolean>;
}

/** Keeps the source-leave fence attached to one admitted formal transfer. */
export class ZLinkFormalRemoteActorTransferRegistry {
  private readonly transfers = new Map<string, {
    readonly transfer: ZLinkFormalRemoteActorTransfer;
    readonly resolveSourceLeaveTerminal: (succeeded: boolean) => void;
  }>();

  has(actorId: string): boolean {
    return this.transfers.has(actorId);
  }

  get(actorId: string): ZLinkFormalRemoteActorTransfer | undefined {
    return this.transfers.get(actorId)?.transfer;
  }

  begin(input: {
    readonly actor: ZLinkActor;
    readonly spotId: RoutingId;
    readonly transferId: string;
    readonly handoffBacklog: readonly ZLinkActorHandoffPacket[];
    readonly deferredJoinRoot?: ZLinkDeferredJoinAcceptedRoot;
  }): ZLinkFormalRemoteActorTransfer {
    let resolveSourceLeaveTerminal!: (succeeded: boolean) => void;
    const sourceLeaveTerminal = new Promise<boolean>((resolve) => {
      resolveSourceLeaveTerminal = resolve;
    });
    const transfer: ZLinkFormalRemoteActorTransfer = {
      ...input,
      sourceLeaveTerminal
    };
    this.transfers.set(input.actor.context.actorId, {
      transfer,
      resolveSourceLeaveTerminal
    });
    return transfer;
  }

  delete(actorId: string): void {
    this.transfers.delete(actorId);
  }

  completeSourceLeaveTerminal(
    actorId: string,
    transferId: string,
    succeeded: boolean
  ): boolean {
    const pending = this.transfers.get(actorId);
    if (pending === undefined || pending.transfer.transferId !== transferId) {
      return false;
    }
    pending.resolveSourceLeaveTerminal(succeeded);
    return true;
  }
}
