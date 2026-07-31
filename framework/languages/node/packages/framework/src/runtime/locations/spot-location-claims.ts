import type { RoutingId } from '../../contracts/Common';
import type {
  ZLinkAuthoritySnapshot,
  ZLinkAuthorityStoreVersion
} from '../../contracts/Locations';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import {
  ZLinkLocationKind,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  type ZLinkSpotLocation
} from './internal-location-contracts';
import { ZLinkSpotKind } from '../../contracts/Spots';
import { ZLinkLocationKeyCodec } from './key-codec';
import type {
  IZLinkLocationLifecycleRuntime,
  ZLinkOwnershipLostEvent
} from './lifecycle-runtime';
import type { ZLinkAuthorityStore } from './internal-store-contracts';
import { encodeAuthorityKey } from './authority-key-codec';
import { routingIdsEqual } from '../routing-id';

export class ZLinkSpotLocationClaims {
  private readonly spots = new Map<string, TrackedSpot>();

  constructor(
    private readonly runtime: IZLinkLocationLifecycleRuntime,
    private readonly authorityStore?: ZLinkAuthorityStore
  ) {}

  async claim(
    meshName: string,
    spotId: RoutingId,
    spotType: string,
    nodeRid: RoutingId,
    spotKind: ZLinkSpotKind,
    spotGeneration: bigint,
    ownerNodeGeneration: bigint,
    deactivate?: () => Promise<void>
  ): Promise<ZLinkLocationWriteStatus> {
    const row: ZLinkSpotLocation = {
      meshName,
      spotId,
      spotType,
      spotGeneration,
      ownerNodeRid: nodeRid,
      ownerNodeGeneration,
      spotKind,
      ownerId: '',
      leaseGeneration: 0n,
      updatedAt: new Date(0)
    };
    const result = await this.runtime.writeSpot(row, ZLinkLocationWriteIntent.NewClaim);
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.spots.set(
        ZLinkLocationKeyCodec.encodeSpotKey({ meshName, spotId }),
        { kind: 'legacy', generation: result.generation, deactivate }
      );
    }
    return result.status;
  }

  trackInstanceAuthority(input: ZLinkTrackedInstanceAuthority): void {
    if (this.authorityStore === undefined) {
      return;
    }
    this.spots.set(
      ZLinkLocationKeyCodec.encodeSpotKey({
        meshName: input.meshName,
        spotId: input.spotId
      }),
      { kind: 'authority', ...input }
    );
  }

  async release(meshName: string, spotId: RoutingId): Promise<void> {
    const key = { meshName, spotId };
    const canonical = ZLinkLocationKeyCodec.encodeSpotKey(key);
    const tracked = this.spots.get(canonical);
    if (tracked === undefined) {
      return;
    }
    if (tracked.kind === 'legacy') {
      this.spots.delete(canonical);
      await this.runtime.removeSpot(key, tracked.generation);
      return;
    }
    await this.releaseAuthority(tracked);
    if (this.spots.get(canonical) === tracked) {
      this.spots.delete(canonical);
    }
  }

  onOwnershipLost(event: ZLinkOwnershipLostEvent): void {
    if (event.kind !== ZLinkLocationKind.Spot) {
      return;
    }
    const tracked = this.spots.get(event.key);
    this.spots.delete(event.key);
    if (tracked?.deactivate !== undefined) {
      void tracked.deactivate().catch(() => undefined);
    }
  }

  private async releaseAuthority(tracked: TrackedAuthoritySpot): Promise<void> {
    const store = this.authorityStore;
    if (store === undefined) return;
    const key = encodeAuthorityKey('instance_spot', String(tracked.spotId));
    let result = await store.compareExchangeAuthority(
      key,
      { value: tracked.storeVersion } as ZLinkAuthorityStoreVersion,
      { kind: 'delete' }
    );
    if (
      result.kind === 'conflict'
      && result.current.kind === 'snapshot'
      && matchesTrackedAuthority(result.current, tracked)
    ) {
      result = await store.compareExchangeAuthority(
        key,
        result.current.storeVersion,
        { kind: 'delete' }
      );
    }
    if (result.kind === 'deleted' || (
      result.kind === 'conflict' && result.current.kind === 'missing'
    )) {
      return;
    }
    if (result.kind === 'generationExhausted') {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.InvalidOperation,
        `Instance Spot '${String(tracked.spotId)}' authority generation is exhausted.`
      );
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.InvalidOperation,
      `Instance Spot '${String(tracked.spotId)}' authority is owned by another runtime.`,
      true
    );
  }
}

export interface ZLinkTrackedInstanceAuthority {
  readonly meshName: string;
  readonly spotId: RoutingId;
  readonly stableType: string;
  readonly nodeRid: RoutingId;
  readonly nodeGeneration: bigint;
  readonly objectGeneration: bigint;
  readonly authorityOwnerGeneration: bigint;
  readonly ownerId: string;
  readonly ownerLeaseGeneration: bigint;
  readonly storeVersion: string;
  readonly deactivate?: () => Promise<void>;
}

interface TrackedLegacySpot {
  readonly kind: 'legacy';
  readonly generation: bigint;
  readonly deactivate?: () => Promise<void>;
}

interface TrackedAuthoritySpot extends ZLinkTrackedInstanceAuthority {
  readonly kind: 'authority';
}

type TrackedSpot = TrackedLegacySpot | TrackedAuthoritySpot;

function matchesTrackedAuthority(
  snapshot: ZLinkAuthoritySnapshot,
  tracked: TrackedAuthoritySpot
): boolean {
  return snapshot.objectGeneration === tracked.objectGeneration
    && snapshot.authorityOwnerGeneration === tracked.authorityOwnerGeneration
    && snapshot.ownerId === tracked.ownerId
    && snapshot.ownerLeaseGeneration === tracked.ownerLeaseGeneration
    && snapshot.allocation.state === 'active'
    && snapshot.allocation.objectKind === 'instance_spot'
    && snapshot.allocation.stableType === tracked.stableType
    && snapshot.allocation.descriptor.meshName === tracked.meshName
    && routingIdsEqual(snapshot.allocation.descriptor.rid, tracked.nodeRid)
    && snapshot.allocation.descriptorLifecycleGeneration === tracked.nodeGeneration;
}
