import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkMessage,
  ZLinkMessageSerializer,
  ZLinkSpot
} from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendActorRef, ZLinkBackendSpotNode } from '../backend';
import {
  ZLINK_REMOTE_BOUND_SESSION_OWNERSHIP_PACKET,
  toFrameworkActorRef,
  type ZLinkActorHandoffCoordinator,
  type ZLinkActorRoutedJoinTransport,
  type ZLinkActorTransferRegistry,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import type { ZLinkActorRuntimeState } from '../actors/actor-runtime-state';
import { ZLinkActorRetryDelay } from '../actors/actor-retry-delay';
import { encodeRemoteBoundSessionOwnershipPayload } from '../actors/bound-session-wire';
import type { ZLinkLocationLifecycle } from '../locations';
import { wrapFrameworkPayloadMessage } from '../messaging/payload-codec';
import type { DefaultZLinkSpotManager } from '../spots';
import type { ZLinkNativeActorJoinSnapshot } from '../spots/spot-runtime-ports';

export interface ZLinkActorTransferRuntimeActorManager {
  getState(actorId: string): ZLinkActorRuntimeState | undefined;
  getOrCreateActor(actorId: string, actorType: string, signal?: AbortSignal): Promise<ZLinkActor>;
  getOrCreateWithNativeRef(
    actorId: string,
    actorType: string,
    actorRef: ZLinkBackendActorRef,
    actorCreateRequest?: unknown,
    signal?: AbortSignal
  ): Promise<ZLinkActor>;
  materializeTransferredActor(
    actorId: string,
    actorType: string,
    adapterKey: string | undefined,
    transferState: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<{ readonly actor: ZLinkActor; readonly actorRef: ActorRef }>;
  rollbackTransferredActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkActorTransferRuntimeOptions {
  readonly routeTransport: ZLinkActorRoutedJoinTransport;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly spotManager: () => DefaultZLinkSpotManager | undefined;
  readonly actorManager: () => ZLinkActorTransferRuntimeActorManager | undefined;
  readonly primarySpotNode: () => ZLinkBackendSpotNode;
  readonly notifyEntrySpotActorLeft: (actor: ZLinkActor, signal?: AbortSignal) => Promise<void>;
  readonly restoreEntrySpotActorJoined: (actor: ZLinkActor, signal?: AbortSignal) => Promise<void>;
  readonly locationLifecycle: () => ZLinkLocationLifecycle | undefined;
  readonly actorHandoff: ZLinkActorHandoffCoordinator;
  readonly actorTransferRegistry: ZLinkActorTransferRegistry;
  readonly clearRemoteActorPacketTarget: (actorId: string) => void;
  readonly reportPostCommitError?: (error: unknown) => void;
  readonly shutdownSignal?: () => AbortSignal | undefined;
  readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
}

export class ZLinkActorTransferRuntime {
  private readonly sourceDepartureTasks = new Map<string, Promise<void>>();

  constructor(private readonly options: ZLinkActorTransferRuntimeOptions) {}

  private async prepareSourceActorLeave(
    actor: ZLinkActor,
    sourceSpotRid: RoutingId | undefined,
    signal?: AbortSignal
  ): Promise<void> {
    if (sourceSpotRid !== undefined) {
      const manager = this.options.spotManager();
      if (manager !== undefined) {
        await manager.prepareActorLeaveForTransfer(sourceSpotRid, actor, signal);
        return;
      }
    }
    await this.options.notifyEntrySpotActorLeft(actor, signal);
  }

  private async restoreSourceActor(
    actor: ZLinkActor,
    sourceSpotRid: RoutingId | undefined,
    signal?: AbortSignal
  ): Promise<void> {
    if (sourceSpotRid !== undefined) {
      const manager = this.options.spotManager();
      if (manager !== undefined) {
        await manager.restoreActorAfterFailedTransfer(sourceSpotRid, actor, signal);
        return;
      }
    }
    await this.options.restoreEntrySpotActorJoined(actor, signal);
  }

  private async beginSourceActorMove(actor: ZLinkActor, state: ZLinkActorRuntimeState): Promise<void> {
    state.beginMove();
    this.options.actorHandoff.begin(actor.actorId, state.nativeActorRef?.generation ?? 0n);
    try {
      if (state.spotRid !== undefined) {
        await this.options.spotManager()?.beginActorTransfer(state.spotRid, actor.actorId);
      }
    } catch (error) {
      this.options.actorHandoff.cancel(actor.actorId);
      state.endMove();
      throw error;
    }
  }

  private async cancelSourceActorMove(actor: ZLinkActor, state: ZLinkActorRuntimeState): Promise<void> {
    try {
      if (state.spotRid !== undefined) {
        await this.options.spotManager()?.cancelActorTransfer(state.spotRid, actor.actorId);
      }
    } finally {
      this.options.actorHandoff.cancel(actor.actorId);
      state.endMove();
    }
  }

  async prepareSource(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    signal?: AbortSignal
  ) {
    const transferStarted = process.hrtime.bigint();
    await this.beginSourceActorMove(actor, state);
    const sourceSpotRid = state.spotRid;
    let sourceLeaveStarted = false;
    try {
      const transfer = await this.options.actorTransferRegistry.transferOut(actor, signal);
      this.options.metrics?.histogram(
        'zlink.actor.transfer.pending_requests.count',
        this.options.actorHandoff.snapshot(actor.actorId).length,
        '{request}'
      );
      sourceLeaveStarted = true;
      await this.prepareSourceActorLeave(actor, sourceSpotRid, signal);
      let phase: 'prepared' | 'committed' | 'rolledBack' = 'prepared';
      return {
        ...transfer,
        handoffBacklog: this.options.actorHandoff.snapshot(actor.actorId),
        commit: (target: Parameters<ZLinkActorHandoffCoordinator['complete']>[1], targetActorRef: ActorRef, results: Parameters<ZLinkActorHandoffCoordinator['complete']>[3]) => {
          if (phase !== 'prepared') return;
          phase = 'committed';
          this.options.metrics?.count('zlink.actor.transfers');
          this.options.metrics?.duration(
            'zlink.actor.transfer.duration',
            Number(process.hrtime.bigint() - transferStarted) / 1e9
          );
          try {
            this.options.actorHandoff.complete(actor.actorId, target, targetActorRef, results);
          } catch (error) {
            // The target has already committed. Local forwarding setup is now
            // post-commit work and must not turn the accepted transfer into a
            // source rollback that can no longer undo the target.
            this.options.reportPostCommitError?.(error);
          } finally {
            this.scheduleSourceDeparture(actor, sourceSpotRid);
          }
        },
        rollback: async () => {
          if (phase !== 'prepared') return;
          phase = 'rolledBack';
          await this.cancelSourceActorMove(actor, state);
          await this.restoreSourceActor(actor, sourceSpotRid);
        }
      };
    } catch (error) {
      try {
        await this.cancelSourceActorMove(actor, state);
        if (sourceLeaveStarted) await this.restoreSourceActor(actor, sourceSpotRid);
      } catch (rollbackError) {
        throw new AggregateError([error, rollbackError], 'Actor source leave and rollback both failed.');
      }
      throw error;
    }
  }

  private scheduleSourceDeparture(actor: ZLinkActor, sourceSpotRid: RoutingId | undefined): void {
    if (this.sourceDepartureTasks.has(actor.actorId)) return;
    const task = this.finishSourceDeparture(actor, sourceSpotRid)
      .finally(() => this.sourceDepartureTasks.delete(actor.actorId));
    this.sourceDepartureTasks.set(actor.actorId, task);
  }

  private async finishSourceDeparture(actor: ZLinkActor, sourceSpotRid: RoutingId | undefined): Promise<void> {
    const retry = new ZLinkActorRetryDelay();
    while (this.options.shutdownSignal?.()?.aborted !== true) {
      try {
        if (sourceSpotRid !== undefined) {
          await this.options.spotManager()?.commitActorLeaveAfterTransfer(sourceSpotRid, actor.actorId);
        }
        return;
      } catch (error) {
        this.options.reportPostCommitError?.(error);
        if (!await retry.wait(this.options.shutdownSignal?.())) return;
      }
    }
  }

  async getOrCreateRoutedActor(
    actorId: string,
    actorType: string,
    actorRef?: ActorRef,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    actorCreateRequest?: Message,
    signal?: AbortSignal
  ): Promise<{ readonly actor: ZLinkActor; readonly actorRef: ZLinkBackendActorRef }> {
    const actorManager = this.requireActorManager('Routed actor join requires ZLINK_ACTOR_MANAGER.');
    const actor = actorRef === undefined
      ? await actorManager.getOrCreateActor(actorId, actorType, signal)
      : await actorManager.getOrCreateWithNativeRef(
          actorId,
          actorType,
          actorRef as unknown as ZLinkBackendActorRef,
          actorCreateRequest === undefined
            ? undefined
            : wrapFrameworkPayloadMessage(actorCreateRequest, this.options.messageSerializers),
          signal
        );
    const state = actorManager.getState(actorId);
    if (state === undefined) {
      throw new Error(`Actor '${actorId}' state was not created.`);
    }
    if (actorRef !== undefined) {
      state.setNativeActorRef(actorRef as unknown as ZLinkBackendActorRef);
      state.setRemoteBoundSessionTarget(remoteBoundSessionTarget);
      return { actor, actorRef: actorRef as unknown as ZLinkBackendActorRef };
    }
    return { actor, actorRef: state.ensureNativeActorRef(this.options.primarySpotNode()) };
  }

  async materializeRoutedActor(
    actorId: string,
    actorType: string,
    adapterKey: string | undefined,
    transferState: Message,
    actorEntryNodeRid: RoutingId | undefined,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    signal?: AbortSignal
  ): Promise<{ readonly actor: ZLinkActor; readonly actorRef: ZLinkBackendActorRef }> {
    const actorManager = this.requireActorManager('Routed actor transfer requires ZLINK_ACTOR_MANAGER.');
    const materialized = await actorManager.materializeTransferredActor(
      actorId,
      actorType,
      adapterKey,
      wrapFrameworkPayloadMessage(transferState, this.options.messageSerializers),
      signal
    );
    const state = actorManager.getState(actorId);
    if (state === undefined) {
      throw new Error(`Actor '${actorId}' transfer state was not created.`);
    }
    if (actorEntryNodeRid !== undefined) state.setEntryNodeRid(actorEntryNodeRid);
    state.setRemoteBoundSessionTarget(remoteBoundSessionTarget);
    return {
      actor: materialized.actor,
      actorRef: materialized.actorRef as unknown as ZLinkBackendActorRef
    };
  }

  commitRoutedActor(actor: ZLinkActor, spotRid: RoutingId, spot: ZLinkSpot): void {
    this.options.actorManager()?.getState(actor.actorId)?.setJoinedSpot(spotRid, spot);
  }

  async claimRoutedActorLocation(actor: ZLinkActor, spotRid: RoutingId, spotMeshName: string): Promise<void> {
    const state = this.options.actorManager()?.getState(actor.actorId);
    const actorType = state?.actorType;
    const actorRef = state?.nativeActorRef;
    const lifecycle = this.options.locationLifecycle();
    if (state === undefined || actorType === undefined || actorRef === undefined || lifecycle === undefined) {
      return;
    }
    const claim = await lifecycle.takeoverActorJoinedSpot(
      actorType,
      actor.actorId,
      toFrameworkActorRef(actorRef),
      spotMeshName,
      spotRid,
      async () => state.clearAfterDestroy()
    );
    if (claim.status === 'conflict') {
      throw new Error(`Actor '${actor.actorId}' target location takeover was rejected.`);
    }
    if (claim.claimed !== undefined) {
      state.setLocationGeneration(claim.claimed.generation);
    }
    state.markLocationOwned();
  }

  async claimNativeActorLocation(
    actor: ZLinkActor,
    spotRid: RoutingId,
    spotMeshName: string
  ): Promise<ZLinkNativeActorJoinSnapshot> {
    const state = this.options.actorManager()?.getState(actor.actorId);
    const snapshot = {
      spotRid: state?.spotRid,
      spot: state?.spot,
      spotMeshName: this.options.locationLifecycle()?.actorLocationSnapshot(actor.actorId)?.spotMeshName
    };
    await this.claimRoutedActorLocation(actor, spotRid, spotMeshName);
    return snapshot;
  }

  async publishRoutedActorOwnership(actor: ZLinkActor): Promise<void> {
    const state = this.options.actorManager()?.getState(actor.actorId);
    const actorRef = state?.nativeActorRef;
    const generation = state?.locationGeneration;
    if (state === undefined || actorRef === undefined || generation === undefined) return;
    await this.publishBoundSessionOwnership(
      actor.actorId,
      actorRef,
      generation,
      state.remoteBoundSessionTarget
    );
  }

  clearRoutedActor(actor: ZLinkActor): void {
    this.options.actorManager()?.getState(actor.actorId)?.clearJoinedSpot();
    this.options.clearRemoteActorPacketTarget(actor.actorId);
  }

  async rollbackNativeActorJoin(
    actor: ZLinkActor,
    snapshot: ZLinkNativeActorJoinSnapshot
  ): Promise<void> {
    const state = this.options.actorManager()?.getState(actor.actorId);
    const actorType = state?.actorType;
    const actorRef = state?.nativeActorRef;
    const lifecycle = this.options.locationLifecycle();
    if (snapshot.spotRid === undefined) state?.clearJoinedSpot();
    else state?.setJoinedSpot(snapshot.spotRid, snapshot.spot);
    if (state?.ownsLocation !== true || actorType === undefined || lifecycle === undefined) return;
    if (snapshot.spotRid === undefined) {
      await lifecycle.notifyActorLeftSpot(actorType, actor.actorId);
      return;
    }
    if (actorRef === undefined) {
      throw new Error(`Actor '${actor.actorId}' cannot restore its previous SPOT location without a native ref.`);
    }
    if (snapshot.spotMeshName === undefined) {
      throw new Error(`Actor '${actor.actorId}' cannot restore its previous SPOT location without its mesh name.`);
    }
    const restored = await lifecycle.takeoverActorJoinedSpot(
      actorType,
      actor.actorId,
      toFrameworkActorRef(actorRef),
      snapshot.spotMeshName,
      snapshot.spotRid,
      async () => state.clearAfterDestroy()
    );
    if (restored.status === 'conflict') {
      throw new Error(`Actor '${actor.actorId}' previous SPOT location could not be restored.`);
    }
    if (restored.claimed !== undefined) state.setLocationGeneration(restored.claimed.generation);
  }

  async rollbackRoutedActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    const manager = this.options.actorManager();
    const state = manager?.getState(actor.actorId);
    const actorType = state?.actorType;
    const lifecycle = this.options.locationLifecycle();
    let locationError: unknown;
    if (state?.ownsLocation === true && actorType !== undefined && lifecycle !== undefined) {
      try {
        await lifecycle.releaseActor(actorType, actor.actorId);
        state.markLocationReleased();
      } catch (error) {
        locationError = error;
        void lifecycle.releaseActorEventually(actorType, actor.actorId);
      }
    }
    try {
      await manager?.rollbackTransferredActor(actor, signal);
    } catch (rollbackError) {
      if (locationError !== undefined) {
        throw new AggregateError(
          [locationError, rollbackError],
          `Actor '${actor.actorId}' target transfer rollback failed.`
        );
      }
      throw rollbackError;
    }
    if (locationError !== undefined) throw locationError;
  }

  actorEntryNodeRid(actor: ZLinkActor): RoutingId | undefined {
    return this.options.actorManager()?.getState(actor.actorId)?.entryNodeRid;
  }

  private requireActorManager(message: string): ZLinkActorTransferRuntimeActorManager {
    const actorManager = this.options.actorManager();
    if (actorManager === undefined) {
      throw new Error(message);
    }
    return actorManager;
  }

  private async publishBoundSessionOwnership(
    actorId: string,
    actorRef: ZLinkBackendActorRef,
    ownershipGeneration: bigint,
    target: ZLinkRemoteBoundSessionTarget | undefined
  ): Promise<void> {
    if (target === undefined) {
      return;
    }
    try {
      await this.options.routeTransport.sendToSpot(
        {
          routerChannelId: target.routerChannelId,
          targetNodeRid: target.targetNodeRid,
          spotRid: target.spotRid,
          spotKind: ZLinkSpotKind.Entry
        },
        encodeRemoteBoundSessionOwnershipPayload({
          actorId,
          actorNodeRid: String(actorRef.nodeRid),
          actorNodeRidHex: (actorRef.nodeRid as { toHex?: () => string }).toHex?.(),
          actorGeneration: actorRef.generation.toString(),
          actorOwnershipGeneration: ownershipGeneration.toString()
        }),
        { packetName: ZLINK_REMOTE_BOUND_SESSION_OWNERSHIP_PACKET }
      );
    } catch (error) {
      throw new Error(
        `Actor '${actorId}' bound-session ownership update failed on route ` +
        `'${target.routerChannelId}' to '${String(target.targetNodeRid)}'.`,
        { cause: error }
      );
    }
  }
}
