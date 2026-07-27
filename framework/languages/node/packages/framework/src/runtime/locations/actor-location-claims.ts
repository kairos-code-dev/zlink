import type { ActorRef, RoutingId } from '../../contracts/Common';
import {
  ZLinkLocationKind,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  type ZLinkActorLocation
} from './internal-location-contracts';
import type { ZLinkActorLocationStore } from './internal-store-contracts';
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
  readonly generation?: bigint;
}

export interface ZLinkActorClaimActivation<TActor> {
  readonly activated?: TActor;
  readonly existingLocation?: ZLinkActorLocation;
  readonly generation?: bigint;
}

export class ZLinkActorLocationClaims {
  private readonly actors = new Map<string, TrackedActor>();

  constructor(
    private readonly runtime: IZLinkLocationLifecycleRuntime,
    private readonly actorStore: ZLinkActorLocationStore,
    private readonly entryMeshName: string
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
      return { activated: await activate(), generation: claim.generation };
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
    const key = { meshName: this.entryMeshName, actorId };
    const canonical = ZLinkLocationKeyCodec.encodeActorKey(key);
    if (this.actors.has(canonical)) {
      return { status: ZLinkActorClaimStatus.AlreadyOwned };
    }

    const row: ZLinkActorLocation = {
      meshName: this.entryMeshName,
      actorType: normalizedType,
      actorId,
      actorRef: {
        actorId,
        objectGeneration: 1n,
        meshName: this.entryMeshName,
        nodeRid
      },
      ownerNodeRid: nodeRid,
      ownerNodeGeneration: 0n,
      spotKind: ZLinkSpotKind.Entry,
      spotId: nodeRid,
      spotGeneration: 0n,
      membershipEpoch: 0n,
      ownerId: '',
      leaseGeneration: 0n,
      updatedAt: new Date(0)
    };
    const result = await this.runtime.writeActor(row, ZLinkLocationWriteIntent.NewClaim);
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      const claimed: ZLinkActorLocation = {
        ...row,
        ownerId: this.runtime.ownerId,
        updatedAt: result.updatedAt
      };
      this.actors.set(canonical, {
        row: claimed,
        generation: result.generation,
        deactivate
      });
      return { status: ZLinkActorClaimStatus.Claimed, claimed, generation: result.generation };
    }

    if (result.status === ZLinkLocationWriteStatus.RejectedConflict) {
      return {
        status: ZLinkActorClaimStatus.Conflict,
        existing: await this.actorStore.resolveActor(key)
      };
    }

    return { status: ZLinkActorClaimStatus.Conflict };
  }

  async setRef(
    actorType: string,
    actorId: string,
    actorRef: ActorRef,
    ownerNodeGeneration: bigint
  ): Promise<void> {
    await this.renew(actorType, actorId, (row) => ({
      ...row,
      actorRef,
      ownerNodeRid: actorRef.nodeRid,
      ownerNodeGeneration,
      spotId: row.spotKind === ZLinkSpotKind.Entry ? actorRef.nodeRid : row.spotId,
      spotGeneration: row.spotKind === ZLinkSpotKind.Entry ? ownerNodeGeneration : row.spotGeneration
    }));
  }

  async takeoverJoinedSpot(
    actorType: string,
    actorId: string,
    actorRef: ActorRef,
    spotMeshName: string,
    spotId: RoutingId,
    spotGeneration: bigint,
    membershipEpoch: bigint,
    ownerNodeGeneration: bigint,
    deactivate?: () => Promise<void>
  ): Promise<ZLinkActorClaimResult> {
    const normalizedType = ZLinkLocationKeyCodec.normalizeActorType(actorType);
    const key = { meshName: spotMeshName, actorId };
    const canonical = ZLinkLocationKeyCodec.encodeActorKey(key);
    const row: ZLinkActorLocation = {
      meshName: spotMeshName,
      actorType: normalizedType,
      actorId,
      actorRef,
      ownerNodeRid: actorRef.nodeRid,
      ownerNodeGeneration,
      spotKind: ZLinkSpotKind.User,
      spotId,
      spotGeneration,
      membershipEpoch,
      ownerId: '',
      leaseGeneration: 0n,
      updatedAt: new Date(0)
    };
    let result = await this.runtime.writeActor(row, ZLinkLocationWriteIntent.Takeover);
    if (result.status !== ZLinkLocationWriteStatus.Stored) {
      const existing = await this.actorStore.resolveActor(key);
      if (existing === undefined) {
        result = await this.runtime.writeActor(row, ZLinkLocationWriteIntent.NewClaim);
      }
    }
    if (result.status !== ZLinkLocationWriteStatus.Stored) {
      return {
        status: ZLinkActorClaimStatus.Conflict,
        existing: await this.actorStore.resolveActor(key)
      };
    }
    const claimed: ZLinkActorLocation = {
      ...row,
      ownerId: this.runtime.ownerId,
      updatedAt: result.updatedAt
    };
    this.actors.set(canonical, {
      row: claimed,
      generation: result.generation,
      deactivate
    });
    return { status: ZLinkActorClaimStatus.Claimed, claimed, generation: result.generation };
  }

  async notifyJoinedSpot(
    actorType: string,
    actorId: string,
    _spotMeshName: string,
    spotId: RoutingId,
    spotGeneration: bigint,
    membershipEpoch: bigint,
    ownerNodeGeneration: bigint
  ): Promise<void> {
    await this.renew(actorType, actorId, (row) => ({
      ...row,
      spotKind: ZLinkSpotKind.User,
      spotId,
      spotGeneration,
      membershipEpoch,
      ownerNodeGeneration
    }));
  }

  async notifyLeftSpot(
    actorType: string,
    actorId: string,
    entrySpotId: RoutingId,
    entrySpotGeneration: bigint,
    membershipEpoch: bigint,
    ownerNodeGeneration: bigint
  ): Promise<void> {
    await this.renew(actorType, actorId, (row) => ({
      ...row,
      spotKind: ZLinkSpotKind.Entry,
      spotId: entrySpotId,
      spotGeneration: entrySpotGeneration,
      membershipEpoch,
      ownerNodeGeneration
    }));
  }

  async release(_actorType: string, actorId: string): Promise<void> {
    const key = { meshName: this.entryMeshName, actorId };
    const canonical = ZLinkLocationKeyCodec.encodeActorKey(key);
    const tracked = this.actors.get(canonical);
    if (tracked === undefined) {
      return;
    }
    await this.runtime.removeActor(key, tracked.generation);
    if (this.actors.get(canonical) === tracked) {
      this.actors.delete(canonical);
    }
  }

  owns(_actorType: string, actorId: string): boolean {
    return this.actors.has(ZLinkLocationKeyCodec.encodeActorKey({
      meshName: this.entryMeshName,
      actorId
    }));
  }

  snapshot(actorId: string): ZLinkActorLocation | undefined {
    const tracked = this.actors.get(ZLinkLocationKeyCodec.encodeActorKey({
      meshName: this.entryMeshName,
      actorId
    }));
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
      meshName: this.entryMeshName,
      actorId
    });
    const tracked = this.actors.get(canonical);
    if (tracked === undefined) {
      return;
    }
    const candidate = mutate(tracked.row);
    const result = await this.runtime.writeActor(candidate, ZLinkLocationWriteIntent.Renew);
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      tracked.row = { ...candidate, updatedAt: result.updatedAt };
      tracked.generation = result.generation;
      return;
    }
    throw new Error(`Actor location renewal for '${actorId}' was rejected with status ${result.status}.`);
  }
}

interface TrackedActor {
  row: ZLinkActorLocation;
  generation: bigint;
  readonly deactivate?: () => Promise<void>;
}
