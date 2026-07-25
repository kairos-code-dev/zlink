import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkActorJoinOperationId,
  ZLinkAuthorityStore,
  ZLinkMessage,
  ZLinkMessageSerializer,
  ZLinkRelocationStore,
  ZLinkSpot
} from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendActorRef, ZLinkBackendMeshNode } from '../backend';
import {
  ZLINK_REMOTE_BOUND_SESSION_OWNERSHIP_PACKET,
  toFrameworkActorRef,
  type ZLinkActorHandoffCoordinator,
  type ZLinkActorRoutedJoinTransport,
  type ZLinkActorTransferRegistry,
  ZLinkDeferredJoinAcceptedJournal,
  type ZLinkDeferredJoinAcceptedRoot,
  type ZLinkRemoteActorPacketTarget,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import type { ZLinkActorRuntimeState } from '../actors/actor-runtime-state';
import { ZLinkActorRetryDelay } from '../actors/actor-retry-delay';
import { encodeRemoteActorPacketTarget } from '../actors/actor-packet-relay-wire';
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
  readonly primaryMeshNode: () => ZLinkBackendMeshNode;
  readonly notifyEntrySpotActorLeft: (actor: ZLinkActor, signal?: AbortSignal) => Promise<void>;
  readonly restoreEntrySpotActorJoined: (actor: ZLinkActor, signal?: AbortSignal) => Promise<void>;
  readonly locationLifecycle: () => ZLinkLocationLifecycle | undefined;
  readonly actorHandoff: ZLinkActorHandoffCoordinator;
  readonly actorTransferRegistry: ZLinkActorTransferRegistry;
  readonly authorityStore: () => ZLinkAuthorityStore | undefined;
  readonly relocationStore: () => ZLinkRelocationStore | undefined;
  readonly clearRemoteActorPacketTarget: (actorId: string) => void;
  readonly reportPostCommitError?: (error: unknown) => void;
  readonly onSourceDepartureCompleted?: (actorId: string) => void;
  readonly shutdownSignal?: () => AbortSignal | undefined;
  readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
}

export class ZLinkActorTransferRuntime {
  private readonly sourceDepartureTasks = new Map<string, Promise<void>>();
  private readonly coreSourceLeaves = new Map<string, {
    readonly promise: Promise<void>;
    readonly resolve: () => void;
    readonly reject: (error: unknown) => void;
  }>();

  constructor(private readonly options: ZLinkActorTransferRuntimeOptions) {}

  async prepareDeferredJoinAccepted(
    actorId: string,
    operationId: ZLinkActorJoinOperationId,
    actorRef: ActorRef,
    rawReply: Uint8Array,
    signal?: AbortSignal
  ): Promise<ZLinkDeferredJoinAcceptedRoot> {
    return await this.requireDeferredJoinJournal().prepare(
      actorId,
      operationId,
      actorRef,
      rawReply,
      signal
    );
  }

  async recoverDeferredJoinAccepted(
    actorId: string,
    signal?: AbortSignal
  ): Promise<ZLinkDeferredJoinAcceptedRoot | undefined> {
    const authority = this.options.authorityStore();
    const relocation = this.options.relocationStore();
    if (authority === undefined || relocation === undefined) return undefined;
    return await new ZLinkDeferredJoinAcceptedJournal(authority, relocation)
      .recover(actorId, signal);
  }

  async commitAndDeliverDeferredJoinAccepted(
    root: ZLinkDeferredJoinAcceptedRoot,
    actor: ZLinkActor,
    actorRef: ActorRef,
    submitMailbox: <T>(operation: () => Promise<T>) => Promise<T>,
    signal?: AbortSignal
  ): Promise<void> {
    const targetActorRef = this.options.actorManager()
      ?.getState(actor.context.actorId)
      ?.nativeActorRef;
    const currentActorRef = targetActorRef === undefined
      ? actorRef
      : toFrameworkActorRef(targetActorRef);
    let current = root.cursor === 'prepared'
      ? await this.requireDeferredJoinJournal().markCommitted(root, currentActorRef, signal)
      : root;
    let lastError: unknown;
    for (let attempt = 0; attempt < 3; attempt++) {
      try {
        current = await this.requireDeferredJoinJournal().deliver(
          current,
          actor,
          currentActorRef,
          submitMailbox,
          signal
        );
        return;
      } catch (error) {
        lastError = error;
        if (attempt < 2) {
          await new Promise<void>((resolve, reject) => {
            const timer = setTimeout(resolve, 10 << attempt);
            timer.unref();
            signal?.addEventListener('abort', () => {
              clearTimeout(timer);
              reject(signal.reason);
            }, { once: true });
          });
          current = await this.requireDeferredJoinJournal().recover(
            currentActorRef.actorId,
            signal
          ) ?? current;
        }
      }
    }
    throw lastError;
  }

  private requireDeferredJoinJournal(): ZLinkDeferredJoinAcceptedJournal {
    const authority = this.options.authorityStore();
    const relocation = this.options.relocationStore();
    if (authority === undefined || relocation === undefined) {
      throw new Error('Cross-node deferred Actor Join requires Location and Relocation Stores.');
    }
    return new ZLinkDeferredJoinAcceptedJournal(authority, relocation);
  }

  private async prepareSourceActorLeave(
    actor: ZLinkActor,
    sourceSpotId: RoutingId | undefined,
    signal?: AbortSignal
  ): Promise<void> {
    if (sourceSpotId !== undefined) {
      const manager = this.options.spotManager();
      if (manager !== undefined) {
        await manager.prepareActorLeaveForTransfer(sourceSpotId, actor, signal);
        return;
      }
    }
    await this.options.notifyEntrySpotActorLeft(actor, signal);
  }

  private async restoreSourceActor(
    actor: ZLinkActor,
    sourceSpotId: RoutingId | undefined,
    signal?: AbortSignal
  ): Promise<void> {
    if (sourceSpotId !== undefined) {
      const manager = this.options.spotManager();
      if (manager !== undefined) {
        await manager.restoreActorAfterFailedTransfer(sourceSpotId, actor, signal);
        return;
      }
    }
    await this.options.restoreEntrySpotActorJoined(actor, signal);
  }

  private async beginSourceActorMove(actor: ZLinkActor, state: ZLinkActorRuntimeState): Promise<void> {
    state.beginMove();
    this.options.actorHandoff.begin(actor.context.actorId, state.nativeActorRef?.generation ?? 0n);
    try {
      if (state.spotId !== undefined) {
        await this.options.spotManager()?.beginActorTransfer(state.spotId, actor.context.actorId);
      }
    } catch (error) {
      this.options.actorHandoff.cancel(actor.context.actorId);
      state.endMove();
      throw error;
    }
  }

  private async cancelSourceActorMove(actor: ZLinkActor, state: ZLinkActorRuntimeState): Promise<void> {
    try {
      if (state.spotId !== undefined) {
        await this.options.spotManager()?.cancelActorTransfer(state.spotId, actor.context.actorId);
      }
    } finally {
      this.options.actorHandoff.cancel(actor.context.actorId);
      state.endMove();
    }
  }

  async prepareSource(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    signal?: AbortSignal,
    lifecycleAuthority: 'framework' | 'core' = 'framework'
  ) {
    const transferStarted = process.hrtime.bigint();
    await this.beginSourceActorMove(actor, state);
    const sourceSpotId = state.spotId;
    let sourceLeaveStarted = false;
    try {
      const transfer = await this.options.actorTransferRegistry.transferOut(
        actor,
        state.actorType,
        signal
      );
      this.options.metrics?.histogram(
        'zlink.actor.transfer.pending_requests.count',
        this.options.actorHandoff.pendingCount(actor.context.actorId),
        '{request}'
      );
      if (lifecycleAuthority === 'framework') {
        sourceLeaveStarted = true;
        await this.prepareSourceActorLeave(actor, sourceSpotId, signal);
      }
      const sourceLeaveCompletion = lifecycleAuthority === 'core'
        ? this.beginCoreSourceLeave(actor.context.actorId)
        : undefined;
      let phase: 'prepared' | 'committed' | 'rolledBack' = 'prepared';
      return {
        ...transfer,
        handoffBacklog: lifecycleAuthority === 'core'
          ? this.options.actorHandoff.snapshotCoreBacklog(actor.context.actorId)
          : this.options.actorHandoff.snapshot(actor.context.actorId),
        sourceLeaveCompletion,
        commit: (
          target: Parameters<ZLinkActorHandoffCoordinator['complete']>[1],
          targetActorRef: ActorRef,
          results: Parameters<ZLinkActorHandoffCoordinator['complete']>[3],
          releaseLocation = true
        ) => {
          if (phase !== 'prepared') return;
          phase = 'committed';
          this.options.metrics?.count('zlink.actor.transfers');
          this.options.metrics?.duration(
            'zlink.actor.transfer.duration',
            Number(process.hrtime.bigint() - transferStarted) / 1e9
          );
          try {
            this.options.actorHandoff.complete(actor.context.actorId, target, targetActorRef, results);
          } catch (error) {
            // The target has already committed. Local forwarding setup is now
            // post-commit work and must not turn the accepted transfer into a
            // source rollback that can no longer undo the target.
            this.options.reportPostCommitError?.(error);
          } finally {
            this.scheduleSourceDeparture(
              actor,
              sourceSpotId,
              lifecycleAuthority === 'core' && releaseLocation
            );
          }
        },
        rollback: async () => {
          if (phase !== 'prepared') return;
          phase = 'rolledBack';
          this.coreSourceLeaves.delete(actor.context.actorId);
          await this.cancelSourceActorMove(actor, state);
          await this.restoreSourceActor(actor, sourceSpotId);
        }
      };
    } catch (error) {
      try {
        await this.cancelSourceActorMove(actor, state);
        if (sourceLeaveStarted) await this.restoreSourceActor(actor, sourceSpotId);
      } catch (rollbackError) {
        throw new AggregateError([error, rollbackError], 'Actor source leave and rollback both failed.');
      }
      throw error;
    }
  }

  async notifyCoreSourceLeave(actor: ZLinkActor, callback: () => Promise<void>): Promise<void> {
    const pending = this.coreSourceLeaves.get(actor.context.actorId);
    try {
      await callback();
      pending?.resolve();
    } catch (error) {
      pending?.reject(error);
      throw error;
    } finally {
      this.coreSourceLeaves.delete(actor.context.actorId);
    }
  }

  private beginCoreSourceLeave(actorId: string): Promise<void> {
    let resolve!: () => void;
    let reject!: (error: unknown) => void;
    const promise = new Promise<void>((accept, fail) => {
      resolve = accept;
      reject = fail;
    });
    void promise.catch(() => {});
    this.coreSourceLeaves.set(actorId, { promise, resolve, reject });
    return promise;
  }

  private scheduleSourceDeparture(
    actor: ZLinkActor,
    sourceSpotId: RoutingId | undefined,
    releaseLocation: boolean
  ): void {
    if (this.sourceDepartureTasks.has(actor.context.actorId)) return;
    const task = this.finishSourceDeparture(actor, sourceSpotId, releaseLocation)
      .finally(() => this.sourceDepartureTasks.delete(actor.context.actorId));
    this.sourceDepartureTasks.set(actor.context.actorId, task);
  }

  private async finishSourceDeparture(
    actor: ZLinkActor,
    sourceSpotId: RoutingId | undefined,
    releaseLocation: boolean
  ): Promise<void> {
    const retry = new ZLinkActorRetryDelay();
    while (this.options.shutdownSignal?.()?.aborted !== true) {
      try {
        if (sourceSpotId !== undefined) {
          await this.options.spotManager()?.commitActorLeaveAfterTransfer(sourceSpotId, actor.context.actorId);
        }
        if (releaseLocation) {
          const state = this.options.actorManager()?.getState(actor.context.actorId);
          if (state?.actorType !== undefined && state.ownsLocation) {
            await this.options.locationLifecycle()?.releaseActor(
              state.actorType,
              actor.context.actorId
            );
            state.markLocationReleased();
          }
        }
        this.options.onSourceDepartureCompleted?.(actor.context.actorId);
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
    return { actor, actorRef: state.ensureNativeActorRef(this.options.primaryMeshNode()) };
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
    state.setBoundSessionTransferTarget(remoteBoundSessionTarget);
    if (remoteBoundSessionTarget?.bindingGeneration !== undefined) {
      state.setBoundSessionBindingGeneration(remoteBoundSessionTarget.bindingGeneration);
    }
    return {
      actor: materialized.actor,
      actorRef: materialized.actorRef as unknown as ZLinkBackendActorRef
    };
  }

  commitRoutedActor(actor: ZLinkActor, spotId: RoutingId, spot: ZLinkSpot): void {
    const state = this.options.actorManager()?.getState(actor.context.actorId);
    state?.setJoinedSpot(spotId, spot);
    const actorRef = state?.nativeActorRef;
    const binding = state?.boundSessionTransferTarget;
    if (
      actorRef !== undefined
      && binding?.sessionNodeRid !== undefined
      && binding.sessionRid !== undefined
      && binding.bindingGeneration !== undefined
    ) {
      this.options.primaryMeshNode().restoreActorSessionBinding?.(
        actorRef,
        binding.sessionNodeRid,
        binding.sessionRid,
        binding.bindingGeneration
      );
    }
  }

  bindRoutedActorRef(actor: ZLinkActor, actorRef: ActorRef): void {
    const node = this.options.primaryMeshNode();
    const localActorRef = node.actorLookup(actor.context.actorId).actor;
    const targetActorRef = {
      ...actorRef,
      nodeRid: node.status().routingId,
      generation: localActorRef.generation > 0n
        ? localActorRef.generation
        : actorRef.generation
    };
    this.options.actorManager()?.getState(actor.context.actorId)?.setNativeActorRef(
      targetActorRef as unknown as ZLinkBackendActorRef
    );
  }

  async claimRoutedActorLocation(
    actor: ZLinkActor,
    spotId: RoutingId,
    spotMeshName: string,
    joinedLocation?: {
      readonly spotGeneration: bigint;
      readonly membershipEpoch: bigint;
    }
  ): Promise<void> {
    const state = this.options.actorManager()?.getState(actor.context.actorId);
    const actorType = state?.actorType;
    const lifecycle = this.options.locationLifecycle();
    if (state === undefined || actorType === undefined || lifecycle === undefined) {
      return;
    }
    const node = this.options.primaryMeshNode();
    const location = node.actorLookup(actor.context.actorId);
    const spotGeneration = joinedLocation?.spotGeneration ?? location.spotGeneration;
    const membershipEpoch = joinedLocation?.membershipEpoch ?? location.membershipEpoch;
    if (spotGeneration <= 0n || membershipEpoch <= 0n) {
      throw new Error(`Actor '${actor.context.actorId}' committed target location has invalid lifecycle generations.`);
    }
    if (
      joinedLocation === undefined
      && (
        location.spotId === null
        || location.spotId !== spotId
      )
    ) {
      throw new Error(`Actor '${actor.context.actorId}' Core location does not match the committed target SPOT.`);
    }
    const deadline = Date.now() + 5_000;
    const ownerNodeGeneration = node.status().lifecycleGeneration;
    if (ownerNodeGeneration <= 0n) {
      throw new Error(`Actor '${actor.context.actorId}' owner MeshNode has no valid lifecycle generation.`);
    }
    let claim;
    for (;;) {
      claim = await lifecycle.takeoverActorJoinedSpot(
        actorType,
        actor.context.actorId,
        toFrameworkActorRef(state.nativeActorRef ?? location.actor as never),
        spotMeshName,
        spotId,
        spotGeneration,
        membershipEpoch,
        ownerNodeGeneration,
        async () => state.clearAfterDestroy()
      );
      if (claim.status !== 'conflict' || Date.now() >= deadline) {
        break;
      }
      await new Promise<void>((resolve) => setTimeout(resolve, 10));
    }
    if (claim.status === 'conflict') {
      throw new Error(`Actor '${actor.context.actorId}' target location takeover was rejected.`);
    }
    if (claim.generation !== undefined) {
      state.setLocationGeneration(claim.generation);
    }
    state.setJoinedSpot(spotId, state.spot, membershipEpoch);
    state.markLocationOwned();
  }

  async claimNativeActorLocation(
    actor: ZLinkActor,
    spotId: RoutingId,
    spotMeshName: string
  ): Promise<ZLinkNativeActorJoinSnapshot> {
    const state = this.options.actorManager()?.getState(actor.context.actorId);
    const previousLocation = this.options.locationLifecycle()?.actorLocationSnapshot(actor.context.actorId);
    const snapshot = {
      spotId: state?.spotId,
      spot: state?.spot,
      locationSpotId: previousLocation?.spotId,
      spotMeshName: previousLocation?.meshName,
      actorRef: previousLocation?.actorRef,
      spotGeneration: previousLocation?.spotGeneration,
      membershipEpoch: previousLocation?.membershipEpoch,
      ownerNodeGeneration: previousLocation?.ownerNodeGeneration
    };
    await this.claimRoutedActorLocation(actor, spotId, spotMeshName);
    return snapshot;
  }

  async publishRoutedActorOwnership(actor: ZLinkActor): Promise<void> {
    const state = this.options.actorManager()?.getState(actor.context.actorId);
    const actorRef = state?.nativeActorRef;
    const generation = state?.locationGeneration;
    if (state === undefined || actorRef === undefined || generation === undefined) return;
    await this.publishBoundSessionOwnership(
      actor.context.actorId,
      actorRef,
      generation,
      state.remoteBoundSessionTarget,
      state.spotId === undefined || state.remoteBoundSessionTarget === undefined ? undefined : {
        routerChannelId: state.remoteBoundSessionTarget.routerChannelId,
        targetNodeRid: actorRef.nodeRid,
        spotId: state.spotId,
        spotKind: ZLinkSpotKind.User
      }
    );
  }

  clearRoutedActor(actor: ZLinkActor): void {
    this.options.actorManager()?.getState(actor.context.actorId)?.clearJoinedSpot();
    this.options.clearRemoteActorPacketTarget(actor.context.actorId);
  }

  async rollbackNativeActorJoin(
    actor: ZLinkActor,
    snapshot: ZLinkNativeActorJoinSnapshot
  ): Promise<void> {
    const state = this.options.actorManager()?.getState(actor.context.actorId);
    const actorType = state?.actorType;
    const lifecycle = this.options.locationLifecycle();
    if (snapshot.spotId === undefined) state?.clearJoinedSpot();
    else state?.setJoinedSpot(snapshot.spotId, snapshot.spot);
    if (state?.ownsLocation !== true || actorType === undefined || lifecycle === undefined) return;
    if (snapshot.spotId === undefined) {
      if (
        snapshot.locationSpotId === undefined
        || snapshot.spotGeneration === undefined
        || snapshot.membershipEpoch === undefined
        || snapshot.ownerNodeGeneration === undefined
      ) {
        throw new Error(`Actor '${actor.context.actorId}' cannot restore its Entry SPOT location without its exact generation fields.`);
      }
      await lifecycle.notifyActorLeftSpot(
        actorType,
        actor.context.actorId,
        snapshot.locationSpotId,
        snapshot.spotGeneration,
        snapshot.membershipEpoch,
        snapshot.ownerNodeGeneration
      );
      return;
    }
    if (snapshot.actorRef === undefined) {
      throw new Error(`Actor '${actor.context.actorId}' cannot restore its previous SPOT location without a native ref.`);
    }
    if (
      snapshot.spotMeshName === undefined
      || snapshot.spotGeneration === undefined
      || snapshot.membershipEpoch === undefined
      || snapshot.ownerNodeGeneration === undefined
    ) {
      throw new Error(`Actor '${actor.context.actorId}' cannot restore its previous SPOT location without its exact generation fields.`);
    }
    const restored = await lifecycle.takeoverActorJoinedSpot(
      actorType,
      actor.context.actorId,
      snapshot.actorRef,
      snapshot.spotMeshName,
      snapshot.spotId,
      snapshot.spotGeneration,
      snapshot.membershipEpoch,
      snapshot.ownerNodeGeneration,
      async () => state.clearAfterDestroy()
    );
    if (restored.status === 'conflict') {
      throw new Error(`Actor '${actor.context.actorId}' previous SPOT location could not be restored.`);
    }
    if (restored.generation !== undefined) state.setLocationGeneration(restored.generation);
  }

  async rollbackRoutedActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    const manager = this.options.actorManager();
    const state = manager?.getState(actor.context.actorId);
    const actorType = state?.actorType;
    const lifecycle = this.options.locationLifecycle();
    let locationError: unknown;
    if (state?.ownsLocation === true && actorType !== undefined && lifecycle !== undefined) {
      try {
        await lifecycle.releaseActor(actorType, actor.context.actorId);
        state.markLocationReleased();
      } catch (error) {
        locationError = error;
        void lifecycle.releaseActorEventually(actorType, actor.context.actorId);
      }
    }
    try {
      await manager?.rollbackTransferredActor(actor, signal);
    } catch (rollbackError) {
      if (locationError !== undefined) {
        throw new AggregateError(
          [locationError, rollbackError],
          `Actor '${actor.context.actorId}' target transfer rollback failed.`
        );
      }
      throw rollbackError;
    }
    if (locationError !== undefined) throw locationError;
  }

  actorEntryNodeRid(actor: ZLinkActor): RoutingId | undefined {
    return this.options.actorManager()?.getState(actor.context.actorId)?.entryNodeRid;
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
    target: ZLinkRemoteBoundSessionTarget | undefined,
    actorPacketTarget: ZLinkRemoteActorPacketTarget | undefined
  ): Promise<void> {
    if (target === undefined) {
      return;
    }
    try {
      await this.options.routeTransport.sendToSpot(
        {
          routerChannelId: target.routerChannelId,
          targetNodeRid: target.targetNodeRid,
          spotId: target.spotId,
          spotKind: ZLinkSpotKind.Entry
        },
        encodeRemoteBoundSessionOwnershipPayload({
          actorId,
          actorNodeRid: String(actorRef.nodeRid),
          actorNodeRidHex: (actorRef.nodeRid as { toHex?: () => string }).toHex?.(),
          actorGeneration: actorRef.generation.toString(),
          actorOwnershipGeneration: ownershipGeneration.toString(),
          actorPacketTarget: encodeRemoteActorPacketTarget(actorPacketTarget)
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
