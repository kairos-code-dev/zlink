import type { ActorRef, RoutingId } from '../../contracts/Common';
import {
  ZLinkLocationKind,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  type ZLinkActorLocationStore,
  type ZLinkActorLocation
} from '../../contracts/Locations';
import { ZLinkSpotKind } from '../../contracts/Spots';
import { ZLinkLocationKeyCodec } from './key-codec';
import type {
  IZLinkLocationLifecycleRuntime,
  ZLinkOwnershipLostEvent
} from './lifecycle-runtime';

export enum ZLinkActorClaimStatus {
  Claimed = 'claimed',
  AlreadyOwned = 'alreadyOwned',
  Conflict = 'conflict'
}

export interface ZLinkActorClaimResult {
  readonly status: ZLinkActorClaimStatus;
  readonly existing?: ZLinkActorLocation;
  readonly claimed?: ZLinkActorLocation;
}

export interface ZLinkActorClaimActivation<TActor> {
  readonly activated?: TActor;
  readonly existingLocation?: ZLinkActorLocation;
}

export class ZLinkActorLocationClaims {
  private readonly actors = new Map<string, TrackedActor>();

  constructor(
    private readonly runtime: IZLinkLocationLifecycleRuntime,
    private readonly actorStore: ZLinkActorLocationStore,
    private readonly entrySpotMeshName: string
  ) {}

  async executeClaimThenActivate<TActor>(
    actorType: string,
    actorId: string,
    nodeRid: RoutingId,
    deactivate: (() => Promise<void>) | undefined,
    activate: () => Promise<TActor>
  ): Promise<ZLinkActorClaimActivation<TActor>> {
    const claim = await this.claim(actorType, actorId, nodeRid, deactivate);
    if (claim.status === ZLinkActorClaimStatus.AlreadyOwned) {
      return {};
    }
    if (claim.status === ZLinkActorClaimStatus.Conflict) {
      return { existingLocation: claim.existing };
    }
    try {
      return { activated: await activate() };
    } catch (error) {
      await this.release(actorType, actorId);
      throw error;
    }
  }

  async claim(
    actorType: string,
    actorId: string,
    nodeRid: RoutingId,
    deactivate?: () => Promise<void>
  ): Promise<ZLinkActorClaimResult> {
    const normalizedType = ZLinkLocationKeyCodec.normalizeActorType(actorType);
    const key = { actorId };
    const canonical = ZLinkLocationKeyCodec.encodeActorKey(key);
    if (this.actors.has(canonical)) {
      return { status: ZLinkActorClaimStatus.AlreadyOwned };
    }

    const row: ZLinkActorLocation = {
      actorType: normalizedType,
      actorId,
      actorRef: undefined,
      nodeRid,
      generation: 0n,
      locationKind: ZLinkSpotKind.Entry,
      spotMeshName: this.entrySpotMeshName,
      spotRid: undefined,
      ownerId: '',
      updatedAt: new Date(0)
    };
    const result = await this.runtime.writeActor(row, ZLinkLocationWriteIntent.NewClaim);
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      const claimed = {
        ...row,
        generation: result.generation,
        ownerId: this.runtime.ownerId,
        updatedAt: result.updatedAt
      };
      this.actors.set(canonical, {
        row: claimed,
        deactivate
      });
      return { status: ZLinkActorClaimStatus.Claimed, claimed };
    }

    if (result.status === ZLinkLocationWriteStatus.RejectedConflict) {
      return {
        status: ZLinkActorClaimStatus.Conflict,
        existing: await this.actorStore.resolveActor(key)
      };
    }

    return { status: ZLinkActorClaimStatus.Conflict };
  }

  async setRef(actorType: string, actorId: string, actorRef: ActorRef): Promise<void> {
    await this.renew(actorType, actorId, (row) => ({ ...row, actorRef }));
  }

  async takeoverJoinedSpot(
    actorType: string,
    actorId: string,
    actorRef: ActorRef,
    spotMeshName: string,
    spotRid: RoutingId,
    deactivate?: () => Promise<void>
  ): Promise<ZLinkActorClaimResult> {
    const normalizedType = ZLinkLocationKeyCodec.normalizeActorType(actorType);
    const key = { actorId };
    const canonical = ZLinkLocationKeyCodec.encodeActorKey(key);
    const row: ZLinkActorLocation = {
      actorType: normalizedType,
      actorId,
      actorRef,
      nodeRid: actorRef.nodeRid,
      generation: 0n,
      locationKind: ZLinkSpotKind.User,
      spotMeshName,
      spotRid,
      ownerId: '',
      updatedAt: new Date(0)
    };
    const result = await this.runtime.writeActor(row, ZLinkLocationWriteIntent.Takeover);
    if (result.status !== ZLinkLocationWriteStatus.Stored) {
      return {
        status: ZLinkActorClaimStatus.Conflict,
        existing: await this.actorStore.resolveActor(key)
      };
    }
    const claimed = {
      ...row,
      generation: result.generation,
      ownerId: this.runtime.ownerId,
      updatedAt: result.updatedAt
    };
    this.actors.set(canonical, {
      row: claimed,
      deactivate
    });
    return { status: ZLinkActorClaimStatus.Claimed, claimed };
  }

  async notifyJoinedSpot(actorType: string, actorId: string, spotMeshName: string, spotRid: RoutingId): Promise<void> {
    await this.renew(actorType, actorId, (row) => ({
      ...row,
      locationKind: ZLinkSpotKind.User,
      spotMeshName,
      spotRid
    }));
  }

  async notifyLeftSpot(actorType: string, actorId: string): Promise<void> {
    await this.renew(actorType, actorId, (row) => ({
      ...row,
      locationKind: ZLinkSpotKind.Entry,
      spotMeshName: this.entrySpotMeshName,
      spotRid: undefined
    }));
  }

  async release(_actorType: string, actorId: string): Promise<void> {
    const key = { actorId };
    const canonical = ZLinkLocationKeyCodec.encodeActorKey(key);
    const tracked = this.actors.get(canonical);
    if (tracked === undefined) {
      return;
    }
    await this.runtime.removeActor(key, tracked.row.generation);
    if (this.actors.get(canonical) === tracked) {
      this.actors.delete(canonical);
    }
  }

  owns(_actorType: string, actorId: string): boolean {
    return this.actors.has(ZLinkLocationKeyCodec.encodeActorKey({
      actorId
    }));
  }

  snapshot(actorId: string): ZLinkActorLocation | undefined {
    const tracked = this.actors.get(ZLinkLocationKeyCodec.encodeActorKey({ actorId }));
    return tracked === undefined ? undefined : { ...tracked.row };
  }

  onOwnershipLost(event: ZLinkOwnershipLostEvent): void {
    if (event.kind !== ZLinkLocationKind.Actor) {
      return;
    }
    const tracked = this.actors.get(event.key);
    this.actors.delete(event.key);
    if (tracked?.deactivate !== undefined) {
      void tracked.deactivate().catch(() => undefined);
    }
  }

  private async renew(
    _actorType: string,
    actorId: string,
    mutate: (row: ZLinkActorLocation) => ZLinkActorLocation
  ): Promise<void> {
    const canonical = ZLinkLocationKeyCodec.encodeActorKey({
      actorId
    });
    const tracked = this.actors.get(canonical);
    if (tracked === undefined) {
      return;
    }
    const candidate = mutate(tracked.row);
    const result = await this.runtime.writeActor(candidate, ZLinkLocationWriteIntent.Renew);
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      tracked.row = { ...candidate, generation: result.generation, updatedAt: result.updatedAt };
      return;
    }
    throw new Error(`Actor location renewal for '${actorId}' was rejected with status ${result.status}.`);
  }
}

interface TrackedActor {
  row: ZLinkActorLocation;
  readonly deactivate?: () => Promise<void>;
}
