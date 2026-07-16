import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkProviderResolver,
  ZLinkMessage
} from '../../contracts';
import type { ZLinkBackendSpotNode } from '../backend';
import type { ZLinkFrameworkRegistration } from '../configuration';
import type { DefaultZLinkActorManager } from '../actors';
import type { DefaultZLinkSpotManager } from '../spots';
import type { ZLinkSpotRouteResolver } from '../spots/spot-routing-internal';
import {
  DefaultZLinkActorClient,
  ZLinkActorNativeJoinCoordinator,
  type ZLinkActorTransferRegistry,
  type ZLinkActorManagerOptions
} from '../actors';
import type { ZLinkActorHandoffCoordinator } from '../actors';
import { type ZLinkActorRoutedJoinTransport } from '../actors';
import { ZLinkLocalFirstActorJoinCoordinator } from '../actors/local-first-actor-join-coordinator';
import type { ZLinkLocationLifecycle, ZLinkStoreLocationResolvers } from '../locations';
import type {
  ZLinkNativeFallbackBoundSessionPort,
  ZLinkStreamActorLifecyclePort
} from '../streams/stream-binding-runtime-ports';
import { ZLinkNativeFallbackBoundSession } from '../streams/native-fallback-bound-session';
import type { ZLinkActorTransferRuntime } from './actor-transfer-runtime';
import type { ZLinkRuntimeAdmissionGate } from '../admission';
import type { ZLinkRemoteActorPacketTarget } from '../actors';

export interface ZLinkActorRuntimeOptionsFactoryOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly routeTransport: ZLinkActorRoutedJoinTransport;
  readonly streamBindingRuntime: ZLinkStreamActorLifecyclePort & ZLinkNativeFallbackBoundSessionPort;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly spotManager: () => DefaultZLinkSpotManager | undefined;
  readonly actorManager: () => DefaultZLinkActorManager | undefined;
  readonly primarySpotNode: () => ZLinkBackendSpotNode;
  readonly primarySpotNodeOrUndefined: () => ZLinkBackendSpotNode | undefined;
  readonly notifyEntrySpotActorCreated: (
    nodeRid: RoutingId,
    actor: ZLinkActor,
    createRequest: ZLinkMessage,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly locationLifecycle: () => ZLinkLocationLifecycle | undefined;
  readonly primarySpotMeshName: () => string | undefined;
  readonly createLocationSpotRouteResolver: () => ZLinkSpotRouteResolver | undefined;
  readonly createActorLocationResolver: () => ZLinkStoreLocationResolvers | undefined;
  readonly forgetDestroyedActorRef: (actorId: string) => void;
  readonly rememberDestroyedActorRef: (actorId: string, actorRef: ActorRef) => void;
  readonly reportPostCommitError: (error: unknown) => void;
  readonly reportBoundSessionSendError: (error: unknown) => void;
  readonly actorHandoff: ZLinkActorHandoffCoordinator;
  readonly actorTransferRuntime: ZLinkActorTransferRuntime;
  readonly actorTransferRegistry: ZLinkActorTransferRegistry;
  readonly shutdownSignal: () => AbortSignal | undefined;
  readonly metrics: import('../diagnostics').ZLinkRuntimeMetrics;
  readonly traceBoundSessionSend?: (actorId: string, packetName: string) => void;
  readonly flowCreationEnabled?: () => boolean;
  readonly admission: ZLinkRuntimeAdmissionGate;
  readonly actorPacketTargetForState: (actorId: string) => ZLinkRemoteActorPacketTarget | undefined;
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
    | 'actorRefResolver'
    | 'actorCreatedNotifier'
    | 'actorDestroyedCleanup'
    | 'locationLifecycle'
    | 'boundSessionFactory'
    | 'actorTransferRegistry'
    | 'shutdownSignal'
    | 'metrics'
    | 'admission'
  > {
    const actorTransferRegistry = this.options.actorTransferRegistry;
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
        actorTransferRuntime: this.options.actorTransferRuntime,
        native: new ZLinkActorNativeJoinCoordinator({
          node: this.options.primarySpotNode,
          spotRouteResolver: spotRouteResolver ?? this.options.createLocationSpotRouteResolver(),
          routedTransport: this.options.routeTransport,
          remoteActorBinder: (actorRef, signal, force) => force === true
            ? this.options.streamBindingRuntime.refreshActor(actorRef, signal)
            : this.options.streamBindingRuntime.rebindActor(actorRef, signal),
          postCommitErrorReporter: this.options.reportPostCommitError,
          locationLifecycle: this.options.locationLifecycle(),
          sourceTransfer: this.options.actorTransferRuntime,
          messageSerializers: this.options.registration.messageSerializers,
          shutdownSignal: this.options.shutdownSignal()
        })
      }),
      messageSerializers: this.options.registration.messageSerializers,
      actorTransferRegistry,
      shutdownSignal: this.options.shutdownSignal(),
      metrics: this.options.metrics,
      admission: this.options.admission,
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
        localActorProvider: () => this.options.actorManager()?.getState(actorId)?.actor !== undefined,
        remoteBoundSessionTargetProvider: () => this.options.actorManager()?.getState(actorId)?.remoteBoundSessionTarget,
        remoteActorPacketTargetProvider: () => this.options.actorPacketTargetForState(actorId),
        requestTimeoutMs: this.options.registration.requestTimeoutMs,
        actorId,
        onSend: this.options.traceBoundSessionSend,
        reportError: this.options.reportBoundSessionSendError,
        flowCreationEnabled: this.options.flowCreationEnabled
      }),
      actorCreatedNodeRidProvider: () => this.options.primarySpotNodeOrUndefined()?.routingId,
      actorRefResolver: this.options.createActorLocationResolver(),
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
      defaultRequestTimeoutMs: this.options.registration.requestTimeoutMs,
      staleActorRefReporter: (actorId) => this.options.actorHandoff.recordStaleFailure(actorId),
      sendErrorReporter: this.options.reportPostCommitError
    };
  }

}
