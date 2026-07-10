import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkMessage
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendActorRef, ZLinkBackendSpotNode } from '../backend';
import type { ZLinkFrameworkRegistration } from '../configuration';
import { wrapFrameworkPayloadMessage } from '../messaging/payload-codec';
import type { DefaultZLinkSpotManager } from '../spots';
import type { ZLinkSpotRouteResolver } from '../spots/spot-routing-internal';
import {
  DefaultZLinkActorClient,
  type ZLinkActorManagerOptions,
  type ZLinkRemoteActorPacketTarget,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import { type ZLinkActorRoutedJoinTransport } from '../actors';
import {
  ZLinkLazyNativeJoinCoordinator,
  ZLinkLocalFirstActorJoinCoordinator
} from '../actors/local-first-actor-join-coordinator';
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
  readonly locationLifecycle: () => ZLinkLocationLifecycle | undefined;
  readonly createLocationSpotRouteResolver: () => ZLinkSpotRouteResolver | undefined;
  readonly createActorLocationResolver: () => ZLinkStoreLocationResolvers | undefined;
  readonly forgetDestroyedActorRef: (actorId: string) => void;
  readonly rememberDestroyedActorRef: (actorId: string, actorRef: ActorRef) => void;
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
  > {
    const effectiveSpotRouteResolver = spotRouteResolver ?? this.options.createLocationSpotRouteResolver();
    return {
      joinCoordinator: new ZLinkLocalFirstActorJoinCoordinator({
        localSpotManager: this.options.spotManager,
        nativeNode: this.options.primarySpotNode,
        actorBinder: (actorRef, signal, force) => force === true
          ? this.options.streamBindingRuntime.refreshActor(actorRef, signal)
          : this.options.streamBindingRuntime.rebindActor(actorRef, signal),
        native: new ZLinkLazyNativeJoinCoordinator(
          this.options.primarySpotNode,
          effectiveSpotRouteResolver,
          this.options.routeTransport,
          (actorRef, signal, force) => force === true
            ? this.options.streamBindingRuntime.refreshActor(actorRef, signal)
            : this.options.streamBindingRuntime.rebindActor(actorRef, signal),
          this.options.locationLifecycle
        )
      }),
      messageSerializers: this.options.registration.messageSerializers,
      nativeActorNodeProvider: this.options.primarySpotNodeOrUndefined,
      locationLifecycle: this.options.locationLifecycle(),
      boundSessionFactory: (actorId) => new ZLinkNativeFallbackBoundSession({
        runtime: this.options.streamBindingRuntime,
        routedTransport: this.options.routeTransport,
        nodeProvider: this.options.primarySpotNode,
        actorRefProvider: () => this.options.actorManager()?.getState(actorId)?.nativeActorRef as ActorRef | undefined,
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

  commitRoutedActor(actor: ZLinkActor, spotRid: RoutingId, spot: ZLinkSpot): void {
    this.options.actorManager()?.getState(actor.actorId)?.setJoinedSpot(spotRid, spot);
  }

  clearRoutedActor(actor: ZLinkActor, clearRemoteActorPacketTarget: (actorId: string) => void): void {
    const state = this.options.actorManager()?.getState(actor.actorId);
    state?.clearJoinedSpot();
    clearRemoteActorPacketTarget(actor.actorId);
  }

  actorEntryNodeRid(actor: ZLinkActor): RoutingId | undefined {
    return this.options.actorManager()?.getState(actor.actorId)?.nativeActorRef?.nodeRid as RoutingId | undefined;
  }

  actorPacketTarget(actorId: string): ZLinkRemoteActorPacketTarget | undefined {
    return this.options.actorManager()?.getState(actorId)?.remoteActorPacketTarget;
  }
}
