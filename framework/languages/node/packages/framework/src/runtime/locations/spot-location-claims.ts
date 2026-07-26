import type { RoutingId } from '../../contracts/Common';
import {
  ZLinkLocationKind,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  type ZLinkSpotLocation
} from '../../contracts/Locations';
import { ZLinkSpotKind } from '../../contracts/Spots';
import { ZLinkLocationKeyCodec } from './key-codec';
import type {
  IZLinkLocationLifecycleRuntime,
  ZLinkOwnershipLostEvent
} from './lifecycle-runtime';

export class ZLinkSpotLocationClaims {
  private readonly spots = new Map<string, TrackedSpot>();

  constructor(private readonly runtime: IZLinkLocationLifecycleRuntime) {}

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
        { generation: result.generation, deactivate }
      );
    }
    return result.status;
  }

  async release(meshName: string, spotId: RoutingId): Promise<void> {
    const key = { meshName, spotId };
    const canonical = ZLinkLocationKeyCodec.encodeSpotKey(key);
    const tracked = this.spots.get(canonical);
    if (tracked === undefined) {
      return;
    }
    this.spots.delete(canonical);
    await this.runtime.removeSpot(key, tracked.generation);
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
}

interface TrackedSpot {
  readonly generation: bigint;
  readonly deactivate?: () => Promise<void>;
}
