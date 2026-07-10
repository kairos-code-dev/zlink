import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkMessage
} from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendActorRef, ZLinkBackendSpotNode } from '../backend';
import type { ZLinkFrameworkRegistration } from '../configuration';
import { wrapFrameworkPayloadMessage } from '../messaging/payload-codec';
import type { DefaultZLinkSpotManager } from '../spots';
import type { ZLinkSpotRouteResolver } from '../spots/spot-routing-internal';
import {
  DefaultZLinkActorClient,
  ZLinkActorNativeJoinCoordinator,
  ZLinkActorTransferRegistry,
  ZLINK_REMOTE_BOUND_SESSION_OWNERSHIP_PACKET,
  toFrameworkActorRef,
  type ZLinkActorManagerOptions,
  type ZLinkRemoteActorPacketTarget,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import { type ZLinkActorRoutedJoinTransport } from '../actors';
import { ZLinkLocalFirstActorJoinCoordinator } from '../actors/local-first-actor-join-coordinator';
import type { ZLinkActorRuntimeState } from '../actors/actor-runtime-state';
import type { ZLinkLocationLifecycle, ZLinkStoreLocationResolvers } from '../locations';
import type { ZLinkStreamBindingRuntime } from '../streams';
import { ZLinkNativeFallbackBoundSession } from '../streams/native-fallback-bound-session';

export interface ZLinkActorRuntimeOptionsFactoryOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly routeTransport: ZLinkActorRoutedJoinTransport;
  readonly streamBindingRuntime: ZLinkStreamBindingRuntime;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly spotManager: () => DefaultZLinkSpotManager | undefined;
  readonly actorManager: () => ZLinkActorRuntimeOptionsActorManager | undefined;
  readonly primarySpotNode: () => ZLinkBackendSpotNode;
  readonly primarySpotNodeOrUndefined: () => ZLinkBackendSpotNode | undefined;
  readonly notifyEntrySpotActorCreated: (
    nodeRid: RoutingId,
    actor: ZLinkActor,
    createRequest: ZLinkMessage,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly notifyEntrySpotActorLeft: (
    actor: ZLinkActor,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly locationLifecycle: () => ZLinkLocationLifecycle | undefined;
  readonly primarySpotMeshName: () => string | undefined;
  readonly createLocationSpotRouteResolver: () => ZLinkSpotRouteResolver | undefined;
  readonly createActorLocationResolver: () => ZLinkStoreLocationResolvers | undefined;
  readonly forgetDestroyedActorRef: (actorId: string) => void;
  readonly rememberDestroyedActorRef: (actorId: string, actorRef: ActorRef) => void;
  readonly reportPostCommitError: (error: unknown) => void;
}

export interface ZLinkActorRuntimeOptionsActorManager {
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

export class ZLinkActorRuntimeOptionsFactory {
  constructor(private readonly options: ZLinkActorRuntimeOptionsFactoryOptions) {}

  createActorManagerOptions(spotRouteResolver?: ZLinkSpotRouteResolver): Pick<
    ZLinkActorManagerOptions,
    | 'joinCoordinator'
    | 'messageSerializers'
    | 'nativeActorNode'
    | 'nativeActorNodeProvider'
    | 'actorCreatedNodeRidProvider'
    | 'actorCreatedNotifier'
    | 'actorDestroyedCleanup'
    | 'locationLifecycle'
    | 'boundSessionFactory'
    | 'actorTransferRegistry'
  > {
    const actorTransferRegistry = new ZLinkActorTransferRegistry(
      this.options.registration.actorTransferAdapters,
      this.options.providerResolver,
      this.options.registration.messageSerializers
    );
    return {
      joinCoordinator: new ZLinkLocalFirstActorJoinCoordinator({
        localSpotManager: this.options.spotManager,
        nativeNode: this.options.primarySpotNode,
        actorBinder: (actorRef, signal, force) => force === true
          ? this.options.streamBindingRuntime.refreshActor(actorRef, signal)
          : this.options.streamBindingRuntime.rebindActor(actorRef, signal),
        postCommitErrorReporter: this.options.reportPostCommitError,
        locationLifecycle: this.options.locationLifecycle,
        localSpotMeshName: this.options.primarySpotMeshName,
        native: new ZLinkActorNativeJoinCoordinator({
          node: this.options.primarySpotNode,
          spotRouteResolver: spotRouteResolver ?? this.options.createLocationSpotRouteResolver(),
          routedTransport: this.options.routeTransport,
          remoteActorBinder: (actorRef, signal, force) => force === true
            ? this.options.streamBindingRuntime.refreshActor(actorRef, signal)
            : this.options.streamBindingRuntime.rebindActor(actorRef, signal),
          postCommitErrorReporter: this.options.reportPostCommitError,
          locationLifecycle: this.options.locationLifecycle(),
          actorTransferRegistry,
          sourceActorLeaver: (actor, state, signal) => this.notifySourceActorLeft(actor, state, signal),
          sourceActorMoveStarter: (actor, state) => this.beginSourceActorMove(actor, state),
          sourceActorMoveCanceler: (actor, state) => this.cancelSourceActorMove(actor, state),
          messageSerializers: this.options.registration.messageSerializers
        })
      }),
      messageSerializers: this.options.registration.messageSerializers,
      actorTransferRegistry,
      nativeActorNodeProvider: this.options.primarySpotNodeOrUndefined,
      locationLifecycle: this.options.locationLifecycle(),
      boundSessionFactory: (actorId) => new ZLinkNativeFallbackBoundSession({
        runtime: this.options.streamBindingRuntime,
        routedTransport: this.options.routeTransport,
        nodeProvider: this.options.primarySpotNode,
        actorRefProvider: () => {
          const state = this.options.actorManager()?.getState(actorId);
          const actorRef = state?.nativeActorRef as ActorRef | undefined;
          return actorRef === undefined
            ? undefined
            : { ...actorRef, ownershipGeneration: state?.locationGeneration } as ActorRef;
        },
        remoteBoundSessionTargetProvider: () => this.options.actorManager()?.getState(actorId)?.remoteBoundSessionTarget,
        remoteActorPacketTargetProvider: () => this.options.actorManager()?.getState(actorId)?.remoteActorPacketTarget,
        requestTimeoutMs: this.options.registration.requestTimeoutMs,
        actorId
      }),
      actorCreatedNodeRidProvider: () => this.options.primarySpotNodeOrUndefined()?.routingId,
      actorCreatedNotifier: (nodeRid, actor, createRequest, signal) => {
        this.options.forgetDestroyedActorRef(actor.actorId);
        return this.options.notifyEntrySpotActorCreated(nodeRid, actor, createRequest, signal);
      },
      actorDestroyedCleanup: (actorId) => {
        const actorRef = this.options.actorManager()?.getState(actorId)?.nativeActorRef as ActorRef | undefined;
        if (actorRef !== undefined) {
          this.options.rememberDestroyedActorRef(actorId, actorRef);
        }
        this.options.streamBindingRuntime.unbindActor(actorId);
      }
    };
  }

  createActorClientOptions(): ConstructorParameters<typeof DefaultZLinkActorClient>[0] {
    return {
      nodeProvider: this.options.primarySpotNodeOrUndefined,
      locationResolver: this.options.createActorLocationResolver,
      messageSerializers: this.options.registration.messageSerializers,
      defaultRequestTimeoutMs: this.options.registration.requestTimeoutMs
    };
  }

  async notifySourceActorLeft(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    signal?: AbortSignal
  ): Promise<void> {
    const sourceSpotRid = state.spotRid;
    if (sourceSpotRid !== undefined) {
      const manager = this.options.spotManager();
      if (manager !== undefined) {
        await manager.notifyActorLeftAfterTransfer(sourceSpotRid, actor, signal);
        return;
      }
    }
    await this.options.notifyEntrySpotActorLeft(actor, signal);
  }

  async beginSourceActorMove(actor: ZLinkActor, state: ZLinkActorRuntimeState): Promise<void> {
    state.beginMove();
    try {
      if (state.spotRid !== undefined) {
        await this.options.spotManager()?.beginActorTransfer(state.spotRid, actor.actorId);
      }
    } catch (error) {
      state.endMove();
      throw error;
    }
  }

  async cancelSourceActorMove(actor: ZLinkActor, state: ZLinkActorRuntimeState): Promise<void> {
    try {
      if (state.spotRid !== undefined) {
        await this.options.spotManager()?.cancelActorTransfer(state.spotRid, actor.actorId);
      }
    } finally {
      state.endMove();
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
    const actorManager = this.options.actorManager();
    if (actorManager === undefined) {
      throw new Error('Routed actor join requires ZLINK_ACTOR_MANAGER.');
    }
    const actor = actorRef === undefined
      ? await actorManager.getOrCreateActor(actorId, actorType, signal)
      : await actorManager.getOrCreateWithNativeRef(
          actorId,
          actorType,
          actorRef as unknown as ZLinkBackendActorRef,
          actorCreateRequest === undefined
            ? undefined
            : wrapFrameworkPayloadMessage(actorCreateRequest, this.options.registration.messageSerializers),
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
    const localActorRef = state.ensureNativeActorRef(this.options.primarySpotNode());
    return { actor, actorRef: localActorRef };
  }

  async materializeRoutedActor(
    actorId: string,
    actorType: string,
    adapterKey: string | undefined,
    transferState: Message,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    signal?: AbortSignal
  ): Promise<{ readonly actor: ZLinkActor; readonly actorRef: ZLinkBackendActorRef }> {
    const actorManager = this.options.actorManager();
    if (actorManager === undefined) {
      throw new Error('Routed actor transfer requires ZLINK_ACTOR_MANAGER.');
    }
    const materialized = await actorManager.materializeTransferredActor(
      actorId,
      actorType,
      adapterKey,
      wrapFrameworkPayloadMessage(transferState, this.options.registration.messageSerializers),
      signal
    );
    const state = actorManager.getState(actorId);
    if (state === undefined) {
      throw new Error(`Actor '${actorId}' transfer state was not created.`);
    }
    state.setRemoteBoundSessionTarget(remoteBoundSessionTarget);
    return {
      actor: materialized.actor,
      actorRef: materialized.actorRef as unknown as ZLinkBackendActorRef
    };
  }

  commitRoutedActor(actor: ZLinkActor, spotRid: RoutingId, spot: ZLinkSpot): void {
    this.options.actorManager()?.getState(actor.actorId)?.setJoinedSpot(spotRid, spot);
  }

  async finalizeRoutedActor(
    actor: ZLinkActor,
    spotRid: RoutingId,
    spotMeshName: string
  ): Promise<void> {
    const manager = this.options.actorManager();
    const state = manager?.getState(actor.actorId);
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
      async () => {
        state.clearAfterDestroy();
      }
    );
    if (claim.status === 'conflict') {
      throw new Error(`Actor '${actor.actorId}' target location takeover was rejected.`);
    }
    if (claim.claimed !== undefined) {
      state.setLocationGeneration(claim.claimed.generation);
      const boundSessionTarget = state.remoteBoundSessionTarget;
      if (boundSessionTarget !== undefined) {
        try {
          await this.options.routeTransport.sendToSpot(
            {
              routerChannelId: boundSessionTarget.routerChannelId,
              targetNodeRid: boundSessionTarget.targetNodeRid,
              spotRid: boundSessionTarget.spotRid,
              spotKind: ZLinkSpotKind.Entry
            },
            {
              actorId: actor.actorId,
              actorNodeRid: String(actorRef.nodeRid),
              actorNodeRidHex: (actorRef.nodeRid as { toHex?: () => string }).toHex?.(),
              actorGeneration: actorRef.generation.toString(),
              actorOwnershipGeneration: claim.claimed.generation.toString()
            },
            { packetName: ZLINK_REMOTE_BOUND_SESSION_OWNERSHIP_PACKET }
          );
        } catch (error) {
          throw new Error(
            `Actor '${actor.actorId}' bound-session ownership update failed on route ` +
            `'${boundSessionTarget.routerChannelId}' to '${String(boundSessionTarget.targetNodeRid)}'.`,
            { cause: error }
          );
        }
      }
    }
    state.markLocationOwned();
  }

  clearRoutedActor(actor: ZLinkActor, clearRemoteActorPacketTarget: (actorId: string) => void): void {
    const state = this.options.actorManager()?.getState(actor.actorId);
    state?.clearJoinedSpot();
    clearRemoteActorPacketTarget(actor.actorId);
  }

  async rollbackRoutedActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    await this.options.actorManager()?.rollbackTransferredActor(actor, signal);
  }

  actorEntryNodeRid(actor: ZLinkActor): RoutingId | undefined {
    return this.options.actorManager()?.getState(actor.actorId)?.nativeActorRef?.nodeRid as RoutingId | undefined;
  }

  actorPacketTarget(actorId: string): ZLinkRemoteActorPacketTarget | undefined {
    return this.options.actorManager()?.getState(actorId)?.remoteActorPacketTarget;
  }
}
