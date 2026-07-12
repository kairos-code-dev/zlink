import type { ActorRef, RoutingId } from '../../contracts/Common';
import type {
  ZLinkActorLocationStore,
  ZLinkActorLocation,
  ZLinkLocationWriteStatus
} from '../../contracts/Locations';
import { ZLinkSpotKind } from '../../contracts/Spots';
import {
  ZLinkActorLocationClaims,
  type ZLinkActorClaimActivation,
  type ZLinkActorClaimResult
} from './actor-location-claims';
export {
  ZLinkActorClaimStatus,
  type ZLinkActorClaimActivation,
  type ZLinkActorClaimResult
} from './actor-location-claims';
import { ZLinkActorSessionRouteClaims } from './actor-session-route-claims';
import { ZLinkSpotLocationClaims } from './spot-location-claims';
import type {
  IZLinkLocationLifecycleRuntime,
  ZLinkOwnershipLostEvent
} from './lifecycle-runtime';
export type {
  IZLinkLocationLifecycleRuntime,
  ZLinkOwnershipLostEvent
} from './lifecycle-runtime';

export class ZLinkLocationLifecycle {
  private readonly actorClaims: ZLinkActorLocationClaims;
  private readonly actorCleanupTasks = new Map<string, Promise<void>>();
  private readonly spotClaims: ZLinkSpotLocationClaims;
  private readonly actorSessionRoutes: ZLinkActorSessionRouteClaims;
  private disposed = false;
  private readonly ownershipLostHandler = (event: ZLinkOwnershipLostEvent) => this.onOwnershipLost(event);

  constructor(
    private readonly runtime: IZLinkLocationLifecycleRuntime,
    actorStore: ZLinkActorLocationStore,
    entrySpotMeshName = ''
  ) {
    this.actorClaims = new ZLinkActorLocationClaims(runtime, actorStore, entrySpotMeshName);
    this.spotClaims = new ZLinkSpotLocationClaims(runtime);
    this.actorSessionRoutes = new ZLinkActorSessionRouteClaims(runtime);
    this.runtime.addOwnershipLostHandler(this.ownershipLostHandler);
  }

  dispose(): void {
    this.disposed = true;
    this.runtime.removeOwnershipLostHandler(this.ownershipLostHandler);
  }

  async executeActorClaimThenActivate<TActor>(
    actorType: string,
    actorId: string,
    nodeRid: RoutingId,
    deactivate: (() => Promise<void>) | undefined,
    activate: () => Promise<TActor>
  ): Promise<ZLinkActorClaimActivation<TActor>> {
    return await this.actorClaims.executeClaimThenActivate(actorType, actorId, nodeRid, deactivate, activate);
  }

  async claimActor(
    actorType: string,
    actorId: string,
    nodeRid: RoutingId,
    deactivate?: () => Promise<void>
  ): Promise<ZLinkActorClaimResult> {
    return await this.actorClaims.claim(actorType, actorId, nodeRid, deactivate);
  }

  async setActorRef(actorType: string, actorId: string, actorRef: ActorRef): Promise<void> {
    await this.actorClaims.setRef(actorType, actorId, actorRef);
  }

  async takeoverActorJoinedSpot(
    actorType: string,
    actorId: string,
    actorRef: ActorRef,
    spotMeshName: string,
    spotRid: RoutingId,
    deactivate?: () => Promise<void>
  ): Promise<ZLinkActorClaimResult> {
    return await this.actorClaims.takeoverJoinedSpot(
      actorType,
      actorId,
      actorRef,
      spotMeshName,
      spotRid,
      deactivate
    );
  }

  async notifyActorJoinedSpot(actorType: string, actorId: string, spotMeshName: string, spotRid: RoutingId): Promise<void> {
    await this.actorClaims.notifyJoinedSpot(actorType, actorId, spotMeshName, spotRid);
  }

  async notifyActorLeftSpot(actorType: string, actorId: string): Promise<void> {
    await this.actorClaims.notifyLeftSpot(actorType, actorId);
  }

  async releaseActor(actorType: string, actorId: string): Promise<void> {
    await this.actorClaims.release(actorType, actorId);
  }

  releaseActorEventually(actorType: string, actorId: string): Promise<void> {
    const existing = this.actorCleanupTasks.get(actorId);
    if (existing !== undefined) {
      return existing;
    }
    const cleanup = this.retryActorRelease(actorType, actorId)
      .finally(() => this.actorCleanupTasks.delete(actorId));
    this.actorCleanupTasks.set(actorId, cleanup);
    return cleanup;
  }

  ownsActor(actorType: string, actorId: string): boolean {
    return this.actorClaims.owns(actorType, actorId);
  }

  actorLocationSnapshot(actorId: string): ZLinkActorLocation | undefined {
    return this.actorClaims.snapshot(actorId);
  }

  private async retryActorRelease(actorType: string, actorId: string): Promise<void> {
    let retryDelayMs = 50;
    while (!this.disposed) {
      try {
        await this.actorClaims.release(actorType, actorId);
        return;
      } catch {
        await waitForRetry(retryDelayMs);
        retryDelayMs = Math.min(retryDelayMs * 2, 1_000);
      }
    }
    return;
  }

  async claimSpot(
    meshName: string,
    spotRid: RoutingId,
    spotType: string | undefined,
    nodeRid: RoutingId,
    spotKind: ZLinkSpotKind,
    routeEndpoint?: string,
    deactivate?: () => Promise<void>
  ): Promise<ZLinkLocationWriteStatus> {
    return await this.spotClaims.claim(meshName, spotRid, spotType, nodeRid, spotKind, routeEndpoint, deactivate);
  }

  async releaseSpot(meshName: string, spotRid: RoutingId): Promise<void> {
    await this.spotClaims.release(meshName, spotRid);
  }

  async bindActorSessionRoute(sessionRid: RoutingId, actorId: string, ownerNodeRid: RoutingId): Promise<void> {
    await this.actorSessionRoutes.bind(sessionRid, actorId, ownerNodeRid);
  }

  async removeActorSessionRoute(sessionRid: RoutingId): Promise<void> {
    await this.actorSessionRoutes.remove(sessionRid);
  }

  private onOwnershipLost(event: ZLinkOwnershipLostEvent): void {
    this.actorClaims.onOwnershipLost(event);
    this.spotClaims.onOwnershipLost(event);
    this.actorSessionRoutes.onOwnershipLost(event);
  }
}

function waitForRetry(delayMs: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, delayMs));
}
