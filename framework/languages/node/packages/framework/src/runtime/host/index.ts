import { randomBytes, randomUUID } from 'node:crypto';
import {
  Message as BindingMessage,
  RequestResult,
  SubmitResult,
  type MessageLike
} from '@zlink-systems/zlink';
import {
  OperationKind,
  ReceiveKind,
  type ReadyRecord,
  type ReceiveRecord
} from '../foundation/service-runtime-contracts';
import {
  meshActorSessionNodeAdapter,
  ZLinkNodeBackendAdapterFactory
} from '../backend';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkBackendContext
} from '../backend';

const LEGACY_MESH_SEND_TIMEOUT_MS = 1000;
const LEGACY_MESH_SEND_CAPACITY = 4096;
import type { ZLinkFrameworkRegistration } from '../configuration';
import type {
  ActorRef,
  RoutingId,
  ZLinkMeshNodeDescriptor,
  ZLinkClientServerRuntime,
  ZLinkFanoutRuntime,
  ZLinkFrameworkRuntime,
  ZLinkFrameworkLifecycleOptions,
  ZLinkFrameworkRelocationOptions,
  ZLinkFrameworkRelocationResult,
  ZLinkFrameworkRuntimeStatus,
  ZLinkFrameworkTerminationResult,
  ZLinkRouteMeshRuntime
} from '../../contracts';
import type { ZLinkProviderResolver } from '../../contracts/Common/ZLinkProviderResolver';
import type { ZLinkRuntimeEventPublisher } from '../diagnostics';
import type { ZLinkSpotRouteResolver } from '../spots/spot-routing-internal';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkFrameworkRelocationMode,
  ZLinkFrameworkRelocationOutcome,
  ZLinkFrameworkRelocationReason,
  ZLinkFrameworkRuntimeState,
  ZLinkFrameworkTerminationOutcome,
  ZLinkFrameworkTerminationReason,
  ZLinkMessageFlowLogMode,
  ZLinkSpotCreateState
} from '../../contracts';
import { ZLinkSubmitStatus } from '../messaging/submission-result';
import {
  ZLinkRuntimeMessageFlowOutcome as ZLinkMessageFlowOutcome,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind
} from '../../contracts/Dispatch/ZLinkDispatchOptions';
import type { ZLinkMessageFlowControl } from '../../contracts';
import {
  DefaultZLinkChannelRuntimeOptions,
  ZLinkDispatchErrorReporter,
  ZLinkChannelRuntimeManager,
  ZLinkRuntimeChannelTransport,
  ZLinkRuntimeRouteTransport,
  type ZLinkDispatchErrorSink
} from '../channels';
import {
  ZLinkFrameworkExecutionState,
  ZLinkRuntimeTaskErrorSink
} from '../execution';
import {
  createDiagnosticsContext,
  createInboundFlow,
  currentOrCreateFlow,
  DefaultZLinkRuntimeEventPublisher,
  flowIfEnabled,
  runWithFlow,
  type ZLinkDiagnosticsContext,
  type ZLinkMessageFlowModeCell,
  ZLinkRuntimeMetrics
} from '../diagnostics';
import {
  RuntimeEventQueue,
  ZLinkClientServerRuntimeProjection,
  ZLinkFanoutRuntimeProjection
} from '../diagnostics/topology-runtime-projections';
import {
  DefaultZLinkSpotManager,
  ZLinkPublicSpotManager,
  ZLinkRuntimeSpotPublisherTransport,
  ZLinkSpotNodeRuntimeManager,
  type ZLinkDetachedTaskRunner,
  type ZLinkSpotManagerOptions
} from '../spots';
import type {
  DefaultZLinkActorManager,
  ZLinkActorManagerOptions
} from '../actors';
import {
  DefaultZLinkActorClient,
  ZLinkActorHandoffCoordinator,
  ZLinkActorTransferRegistry,
  ZLINK_ACTOR_JOIN_ENTRY_SPOT_RUNTIME,
  decodeRemoteActorSourceLeaveTerminal,
  publishInitialActorAuthority
} from '../actors';
import {
  DefaultZLinkBoundSessionFactory,
  type DefaultZLinkBoundSession,
  type ZLinkStreamPayloadCodec,
  ZLinkStreamBindingRuntime,
  ZLinkStreamRuntimeManager
} from '../streams';
import {
  ZLinkOwnerCleanupError,
  ZLinkAuthoritySpotRouteResolver,
  type ZLinkLocationRuntime,
  type ZLinkStoreLocationResolvers
} from '../locations';
import {
  zlinkDefaultLocationOptions,
  type ZLinkLocationRuntimeQuery
} from '../../contracts/Locations';
import { ZLinkMonitoringRuntime } from './monitoring-runtime';
import { ZLinkActorRuntimeOptionsFactory } from './actor-runtime-options-factory';
import { ZLinkActorTransferRuntime } from './actor-transfer-runtime';
import { ZLinkActorTransferAuthorityRuntime } from './actor-transfer-authority-runtime';
import { ZLinkEntryActorRuntimeService } from './entry-actor-runtime';
import { ZLinkLocationRuntimeOwner } from './location-runtime-owner';
import { MeshRouterResolver } from './mesh-router-resolver';
import { ZLinkBoundSessionRelay } from './bound-session-relay';
import { routingIdsEqual } from '../routing-id';
import { ZLinkSpotRuntimeOptionsFactory } from './spot-runtime-options-factory';
import { ZLinkChannelRuntimeOptionsFactory } from './channel-runtime-options-factory';
import { ZLinkSpotNodeRuntimeOptionsFactory } from './spot-node-runtime-options-factory';
import { rollbackRuntimeStart, stopRuntimeParts } from './runtime-shutdown';
import { ZLinkRuntimeAdmissionGate } from '../admission';
import { ZLinkRouteMeshRuntimeCoordinator } from './route-mesh-runtime';
import { ZLinkConfigurationException } from '../../contracts/Configuration/ConfigurationException';
import { ZLinkMeshSubmitterRegistry } from '../messaging';
import { ZLinkStatefulAuthorityRouteRuntime } from './stateful-authority-route-runtime';
import { ZLinkInstanceActivationAuthority } from './instance-activation-authority';
import type { ServiceAsyncInstanceActivationAuthority } from '../foundation/service-stateful-runtime';
import {
  hasObjectClientCapability,
  ZLinkHostSpotAddressTransport
} from './spot-address-transport';
import { ZLinkUserSpotCreationCoordinator } from './user-spot-creation-coordinator';
import {
  decodeFrameworkPayloadMessage,
  encodeFrameworkPayloadMessage
} from '../messaging/payload-codec';
import { DefaultZLinkRouteMeshRuntimeOptions } from './route-mesh-runtime-options';
import { ZLinkHostServiceRelocationRuntime } from './service-relocation-host-runtime';

export interface ZLinkFrameworkRuntimeLifecycle {
  readonly isStarted: boolean;
  readonly locationRuntimeQuery?: ZLinkLocationRuntimeQuery;
  start(): Promise<void>;
  stop(): Promise<void>;
}

export interface ZLinkFrameworkRuntimeHostOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly lifecycleSink?: string[];
  readonly providerResolver?: ZLinkProviderResolver;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
}

export class ZLinkFrameworkRuntimeHost implements
  ZLinkFrameworkRuntimeLifecycle,
  ZLinkFrameworkRuntime,
  ZLinkMessageFlowControl {
  private readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  private readonly lifecycleSink?: string[];
  private executionState?: ZLinkFrameworkExecutionState;
  private runtimeState = ZLinkFrameworkRuntimeState.Preparing;
  private runtimeSequence = 0n;
  private runtimeDeadline?: Date;
  private runtimeRelocationResult?: ZLinkFrameworkRelocationResult;
  private runtimeTerminationResult?: ZLinkFrameworkTerminationResult;
  private relocationOperation?: Promise<ZLinkFrameworkRelocationResult>;
  private shutdownOperation?: Promise<ZLinkFrameworkTerminationResult>;
  private relocationTargetApplicationVersion?: bigint;
  private readonly runtimeObservers = new Set<RuntimeEventQueue<ZLinkFrameworkRuntimeStatus>>();
  private channelRuntime?: ZLinkChannelRuntimeManager;
  private spotNodeRuntime?: ZLinkSpotNodeRuntimeManager;
  private streamRuntime?: ZLinkStreamRuntimeManager;
  private monitoringRuntime?: ZLinkMonitoringRuntime;
  private statefulAuthorityRoutes?: ZLinkStatefulAuthorityRouteRuntime;
  private readonly locationOwner: ZLinkLocationRuntimeOwner;
  private readonly meshRouters: MeshRouterResolver;
  private readonly boundSessionRelay: ZLinkBoundSessionRelay;
  private readonly actorHandoff: ZLinkActorHandoffCoordinator;
  private readonly actorTransferRegistry: ZLinkActorTransferRegistry;
  private readonly actorTransferRuntime: ZLinkActorTransferRuntime;
  private readonly actorTransferAuthorityRuntime: ZLinkActorTransferAuthorityRuntime;
  private readonly serviceRelocation: ZLinkHostServiceRelocationRuntime;
  private readonly entryActorRuntime: ZLinkEntryActorRuntimeService;
  private actorManager?: DefaultZLinkActorManager;
  private spotManager?: DefaultZLinkSpotManager;
  private registerUserSpotHandlers?: (runtime: ZLinkSpotNodeRuntimeManager) => void;
  private readonly destroyedActorRefs = new Map<string, ActorRef>();
  private readonly runtimeEventPublisher: ZLinkRuntimeEventPublisher;
  private readonly metrics: ZLinkRuntimeMetrics;
  private readonly admission = new ZLinkRuntimeAdmissionGate();
  private readonly meshSubmitters: ZLinkMeshSubmitterRegistry;
  private readonly localMeshRouteInFlight = new Map<string, number>();
  private readonly localMeshRouteCapacity = 4096;
  private cachedLocationSpotRouteResolver?: ZLinkSpotRouteResolver;
  // Shared, runtime-mutable message-flow mode cell — installed once so
  // setMessageFlowMode flips every surface live. Seeded from config at start().
  private readonly messageFlowModeCell: ZLinkMessageFlowModeCell = {
    mode: ZLinkMessageFlowLogMode.ErrorsOnly
  };
  private readonly dispatchErrorReporters = new WeakMap<ZLinkDispatchErrorSink, ZLinkDispatchErrorReporter>();
  private readonly runtimeOrPreStartErrorSink: ZLinkDispatchErrorSink = {
    reportRuntimeTaskException: (taskName: string, error: unknown) =>
      (this.errorSink ?? this.preStartErrorSink).reportRuntimeTaskException(taskName, error)
  };
  private cachedDiagnosticsContext?: ZLinkDiagnosticsContext;
  private readonly preStartErrorSink = new ZLinkRuntimeTaskErrorSink();
  readonly channelTransport = new ZLinkRuntimeChannelTransport(() => this.channelRuntime);
  readonly channelRuntimeOptions = new DefaultZLinkChannelRuntimeOptions(() => this.channelRuntime);
  readonly routeMeshRuntimeOptions =
    new DefaultZLinkRouteMeshRuntimeOptions(() => this.spotNodeRuntime);
  readonly routeTransport: ZLinkRuntimeRouteTransport;
  readonly spotAddressTransport: ZLinkHostSpotAddressTransport;
  readonly spotPublisherTransport = new ZLinkRuntimeSpotPublisherTransport(() => this.spotNodeRuntime);
  readonly streamBindingRuntime: ZLinkStreamBindingRuntime;
  readonly boundSessionFactory: DefaultZLinkBoundSessionFactory;
  readonly spotRouterChannelIdForMesh: (meshName: string) => string;
  readonly routeMeshRuntime: ZLinkRouteMeshRuntime;
  readonly clientServerRuntime: ZLinkClientServerRuntime;
  readonly fanoutRuntime: ZLinkFanoutRuntime;
  private readonly routeMeshCoordinator: ZLinkRouteMeshRuntimeCoordinator;

  constructor(readonly options: ZLinkFrameworkRuntimeHostOptions, internalOptions?: unknown) {
    this.backendAdapterFactory = resolveBackendAdapterFactory(internalOptions);
    this.lifecycleSink = options.lifecycleSink;
    this.meshSubmitters = new ZLinkMeshSubmitterRegistry(
      (meshName) =>
        options.registration.spotNodes.get(meshName)?.router?.sendTimeoutMs
        ?? LEGACY_MESH_SEND_TIMEOUT_MS,
      (meshName) => Math.max(
        1,
        options.registration.spotNodes.get(meshName)?.router?.sendHighWaterMark
        ?? LEGACY_MESH_SEND_CAPACITY
      )
    );
    this.runtimeEventPublisher = options.runtimeEventPublisher ?? new DefaultZLinkRuntimeEventPublisher();
    this.metrics = new ZLinkRuntimeMetrics(options.registration.metrics?.meterProvider);
    this.clientServerRuntime = new ZLinkClientServerRuntimeProjection(() => this.channelRuntime);
    this.fanoutRuntime = new ZLinkFanoutRuntimeProjection(() => this.channelRuntime);
    this.meshRouters = new MeshRouterResolver(options.registration);
    this.routeTransport = new ZLinkRuntimeRouteTransport(
      () => this.channelRuntime,
      (routerChannelId) => this.meshRouters.canUseRouterChannel(routerChannelId),
      () => this.spotNodeRuntime,
      { serializers: options.registration.messageSerializers },
      this.meshSubmitters,
      (meshName, targetNodeRid) =>
        this.meshRouters.classifyManualNodeTarget(meshName, targetNodeRid),
      (meshName, sourceNodeRid, parts) =>
        this.submitLocalMeshRoute(meshName, sourceNodeRid, parts)
    );
    this.spotAddressTransport = new ZLinkHostSpotAddressTransport({
      resolver: () => this.createLocationSpotRouteResolver(),
      routed: this.routeTransport,
      meshNames: () => [...this.options.registration.spotNodes]
        .filter(([, node]) => hasObjectClientCapability(node.objectRole))
        .map(([meshName]) => meshName),
      isMeshConfigured: (meshName) => this.options.registration.spotNodes.has(meshName),
      instanceTypes: (meshName) => Object.keys(
        this.options.registration.spotNodes.get(meshName)?.instanceSpotFactories ?? {}
      ),
      meshNode: (meshName) => this.spotNodeRuntime?.meshNode(meshName),
      completions: (meshName) => this.spotNodeRuntime?.meshCompletionTable(meshName),
      codecs: { serializers: options.registration.messageSerializers },
      defaultRequestTimeoutMs: options.registration.requestTimeoutMs ?? 30_000
    });
    this.spotRouterChannelIdForMesh = this.meshRouters.spotRouterChannelIdByMesh();
    this.locationOwner = new ZLinkLocationRuntimeOwner({
      registration: options.registration,
      runtimeEventPublisher: this.runtimeEventPublisher,
      metrics: this.metrics,
      fallbackNodeRid: `node-${randomUUID()}`
    });
    this.streamBindingRuntime = new ZLinkStreamBindingRuntime({
      streamPayloadCodec: resolveStreamPayloadCodec(options.registration),
      streamCompression: options.registration.streamCompression,
      messageSerializers: options.registration.messageSerializers,
      metrics: this.metrics,
      flowCreationEnabled: () => this.flowCreationEnabled(),
      nativeActorNodeProvider: () => this.spotNodeRuntime?.primaryMeshNode,
      meshSubmitters: this.meshSubmitters,
      nativeActorMeshNameProvider: () => this.meshRouters.primaryMeshName(),
      confirmRemoteActorSessionBinding: (actor, sessionRid, signal) => {
        const sessionNode = this.spotNodeRuntime?.primaryMeshNode;
        return sessionNode === undefined
          ? Promise.resolve()
          : this.boundSessionRelay.actorPackets.confirmRemoteSessionBinding(
              actor,
              sessionNode.status().routingId as never,
              sessionRid,
              signal
            );
      },
      relay: (actor, header, payload, signal) =>
        this.boundSessionRelay.actorPackets.relayActorPacket(actor, header, payload, signal),
      notifyDisconnected: (actor, signal) =>
        this.boundSessionRelay.actorPackets.notifyBoundActorDisconnected(actor, signal)
    });
    this.actorHandoff = new ZLinkActorHandoffCoordinator({
      routedTransport: this.routeTransport,
      messageFollowDurationMs: options.registration.locations.options.messageFollowDurationMs,
      requestTimeoutMs: options.registration.requestTimeoutMs,
      requestSource: () => {
        const owner = this.locationOwner.currentRuntime?.currentOwnerToken;
        const node = this.spotNodeRuntime?.primaryMeshNode?.status();
        return owner === undefined || node === undefined
          ? undefined
          : {
              ownerId: owner.ownerId,
              ownerLeaseGeneration: owner.leaseGeneration,
              nodeRid: String(node.routingId),
              nodeGeneration: node.lifecycleGeneration
            };
      },
      onMarker: (marker, actorId, index) => {
        this.publishActorHandoffEvent({ marker, actorId, index });
      },
      onRequestFrame: (actorId, index, requestSeq, flags) => {
        this.publishActorHandoffEvent({
          marker: 'handoff_request_frame',
          actorId,
          index,
          requestSeq: requestSeq?.toString(),
          flags
        });
      },
      isStaleActorRef: (actorId, actorRef) => {
        const state = this.actorManager?.getState(actorId);
        const current = state?.nativeActorRef;
        if (actorRef === undefined) return state?.remoteActorPacketTarget !== undefined;
        return current !== undefined && (
          current.generation !== actorRef.objectGeneration ||
          !routingIdsEqual(current.nodeRid, actorRef.nodeRid)
        );
      },
      isCurrentHandoffTarget: (actorId, spotId) => {
        const currentSpotId = this.actorManager?.getState(actorId)?.spotId;
        return currentSpotId === spotId;
      }
    });
    this.actorTransferRegistry = new ZLinkActorTransferRegistry(
      options.registration.actorTransferAdapters,
      options.providerResolver,
      options.registration.messageSerializers,
      options.registration.actorFactories
    );
    this.boundSessionFactory = new DefaultZLinkBoundSessionFactory(this.streamBindingRuntime);
    this.boundSessionRelay = new ZLinkBoundSessionRelay({
      requestTimeoutMs: options.registration.requestTimeoutMs,
      routeTransport: this.routeTransport,
      streamBindingRuntime: () => this.streamBindingRuntime,
      meshRouters: this.meshRouters,
      actorManager: () => this.actorManager,
      spotManager: () => this.spotManager,
      spotNodeRuntime: () => this.spotNodeRuntime,
      primaryMeshNode: () => meshActorSessionNodeAdapter(
        this.requirePrimaryMeshNode(),
        this.spotNodeRuntime?.primaryMeshCompletions
      ),
      destroyedActorRefs: this.destroyedActorRefs,
      errorSink: () => this.errorSink ?? this.preStartErrorSink,
      boundSessionFactory: (actorId) => {
        const factory = this.createActorManagerOptions().boundSessionFactory;
        if (factory === undefined) {
          throw new Error('Bound session factory is not configured.');
        }
        return factory(actorId) as DefaultZLinkBoundSession;
      }
    });
    this.entryActorRuntime = new ZLinkEntryActorRuntimeService({
      actorManager: () => this.actorManager,
      spotManager: () => this.spotManager,
      spotNodeRuntime: () => this.spotNodeRuntime,
      streamBindingRuntime: this.streamBindingRuntime,
      boundSessionRelay: this.boundSessionRelay,
      reportPostCommitError: (error) =>
        (this.errorSink ?? this.preStartErrorSink).reportRuntimeTaskException('entry actor commit', error),
      shutdownSignal: () => this.executionState?.abortController.signal
    });
    this.actorTransferRuntime = new ZLinkActorTransferRuntime({
      routeTransport: this.routeTransport,
      messageSerializers: options.registration.messageSerializers,
      spotManager: () => this.spotManager,
      actorManager: () => this.actorManager,
      primaryMeshNode: () => this.requirePrimaryMeshNode(),
      notifyEntrySpotActorLeft: (actor, signal) =>
        this.spotNodeRuntime?.notifyPrimaryEntrySpotActorLeft(actor, signal) ?? Promise.resolve(),
      restoreEntrySpotActorJoined: (actor, signal) =>
        this.spotNodeRuntime?.notifyPrimaryEntrySpotActorJoined(actor, signal) ?? Promise.resolve(),
      locationLifecycle: () => this.locationOwner.currentLifecycle,
      actorHandoff: this.actorHandoff,
      actorTransferRegistry: this.actorTransferRegistry,
      authorityStore: () => this.locationOwner.currentStores?.locationStore,
      relocationStore: () =>
        this.options.registration.locations.relocationStoreInstance,
      clearRemoteActorPacketTarget: (actorId) =>
        this.boundSessionRelay.clearRemoteActorPacketTarget(actorId),
      reportPostCommitError: (error) =>
        (this.errorSink ?? this.preStartErrorSink).reportRuntimeTaskException('source actor departure', error),
      onSourceDepartureCompleted: (actorId) =>
        this.publishActorHandoffEvent({ marker: 'source_cleanup', actorId }),
      shutdownSignal: () => this.executionState?.abortController.signal,
      metrics: this.metrics
    });
    this.serviceRelocation = new ZLinkHostServiceRelocationRuntime({
      registration: options.registration,
      providerResolver: options.providerResolver,
      locationStore: () => this.locationOwner.currentStores?.locationStore,
      relocationStore: () => options.registration.locations.relocationStoreInstance,
      currentOwner: () => this.locationOwner.currentRuntime?.currentOwnerToken,
      liveDescriptors: (meshName, signal) =>
        this.locationOwner.currentRuntime?.listLiveMeshNodes(meshName, signal)
          ?? Promise.resolve([]),
      meshNode: (meshName) => this.spotNodeRuntime?.meshNode(meshName),
      completions: (meshName) => this.spotNodeRuntime?.meshCompletionTable(meshName),
      spotManager: () => this.spotManager,
      actorManager: () => this.actorManager,
      actorTransfer: this.actorTransferRuntime
    });
    this.actorTransferAuthorityRuntime = new ZLinkActorTransferAuthorityRuntime({
      store: () => this.locationOwner.actorTransferStore() as never,
      recoveryOwnerId: () => this.locationOwner.currentRuntime?.ownerId,
      recoveryLeaseTtlMs: {
        ...zlinkDefaultLocationOptions,
        ...options.registration.locations.options
      }.ownerLeaseTtlMs
    });
    this.routeMeshCoordinator = new ZLinkRouteMeshRuntimeCoordinator({
      meshNames: [...options.registration.spotNodes.keys()],
      meshOptions: options.registration.spotNodes,
      meshNode: (meshName) => this.spotNodeRuntime?.meshNode(meshName),
      meshNodeDescriptor: (meshName) =>
        this.spotNodeRuntime?.meshNodeDescriptor(meshName),
      admission: this.admission,
      publishRetiring: (meshName, signal) =>
        this.publishMeshRetiring(meshName, signal),
      rollbackRetiring: (meshName, signal) =>
        this.publishMeshServing(meshName, signal),
      publishDraining: (meshName, signal) =>
        this.publishMeshDraining(meshName, signal),
      publishHostDraining: (signal) => this.publishHostDraining(signal),
      drainResources: (meshName, signal) => this.performMeshDrain(meshName, signal),
      cleanupHostResources: (signal) => this.cleanupOwnerForDrain(signal),
      forceStopResources: (meshName) => this.forceStopMesh(meshName)
    });
    this.routeMeshRuntime = this.routeMeshCoordinator;
  }

  get isStarted(): boolean {
    return this.executionState !== undefined;
  }

  get status(): ZLinkFrameworkRuntimeStatus {
    return {
      state: this.runtimeState,
      isReady: this.runtimeState === ZLinkFrameworkRuntimeState.Serving,
      acceptingWork: this.runtimeState === ZLinkFrameworkRuntimeState.Serving,
      deadline: this.runtimeDeadline,
      relocationResult: this.runtimeRelocationResult,
      terminationResult: this.runtimeTerminationResult,
      sequence: this.runtimeSequence,
      observedAt: new Date()
    };
  }

  observe(signal?: AbortSignal): AsyncIterable<ZLinkFrameworkRuntimeStatus> {
    const queue = new RuntimeEventQueue<ZLinkFrameworkRuntimeStatus>(64, signal);
    this.runtimeObservers.add(queue);
    queue.onClose(() => this.runtimeObservers.delete(queue));
    return queue;
  }

  relocate(options: ZLinkFrameworkRelocationOptions): Promise<ZLinkFrameworkRelocationResult> {
    const effectiveTargetApplicationVersion = validateRelocationOptions(
      options,
      this.options.registration.applicationVersion
    );
    const deadlineMs = options.deadlineMs ?? 30_000;
    if (!Number.isFinite(deadlineMs) || deadlineMs <= 0) {
      return Promise.reject(new RangeError('Relocation deadlineMs must be greater than zero.'));
    }
    if (this.runtimeRelocationResult?.outcome === ZLinkFrameworkRelocationOutcome.Relocated) {
      return Promise.resolve(this.runtimeRelocationResult);
    }
    if (this.shutdownOperation !== undefined) {
      return Promise.resolve({
        mode: options.mode,
        effectiveTargetApplicationVersion,
        outcome: ZLinkFrameworkRelocationOutcome.Blocked,
        reason: ZLinkFrameworkRelocationReason.ShutdownRequested
      });
    }
    if (this.runtimeState !== ZLinkFrameworkRuntimeState.Serving) {
      return Promise.resolve({
        mode: options.mode,
        effectiveTargetApplicationVersion,
        outcome: ZLinkFrameworkRelocationOutcome.Blocked,
        reason: this.runtimeState === ZLinkFrameworkRuntimeState.Relocating
          ? ZLinkFrameworkRelocationReason.OperationInProgress
          : ZLinkFrameworkRelocationReason.RuntimeNotReady
      });
    }
    if (hasUnsupportedManualTopology(this.options.registration)) {
      return Promise.resolve({
        mode: options.mode,
        effectiveTargetApplicationVersion,
        outcome: ZLinkFrameworkRelocationOutcome.Blocked,
        reason: ZLinkFrameworkRelocationReason.ManualTopologyUnsupported
      });
    }
    this.runtimeDeadline = new Date(Date.now() + deadlineMs);
    this.relocationTargetApplicationVersion = effectiveTargetApplicationVersion;
    this.relocationOperation ??= this.runRelocation(
      options.mode,
      effectiveTargetApplicationVersion,
      deadlineMs
    );
    return waitForRuntimeOperation(this.relocationOperation, options.signal);
  }

  shutdown(options?: ZLinkFrameworkLifecycleOptions): Promise<ZLinkFrameworkTerminationResult> {
    const deadlineMs = options?.deadlineMs ?? 30_000;
    if (!Number.isFinite(deadlineMs) || deadlineMs <= 0) {
      return Promise.reject(new RangeError('Shutdown deadlineMs must be greater than zero.'));
    }
    this.runtimeDeadline = new Date(Date.now() + deadlineMs);
    this.shutdownOperation ??= this.runShutdown(deadlineMs);
    return waitForRuntimeOperation(this.shutdownOperation, options?.signal);
  }

  private async runRelocation(
    mode: ZLinkFrameworkRelocationMode,
    effectiveTargetApplicationVersion: bigint,
    deadlineMs: number
  ): Promise<ZLinkFrameworkRelocationResult> {
    if (!this.isStarted) {
      return this.completeRelocation(blockedRelocation(
        mode,
        effectiveTargetApplicationVersion,
        ZLinkFrameworkRelocationReason.RuntimeNotReady
      ));
    }
    try {
      const blocker = await this.preflightAutomaticPeerReadiness(
        deadlineMs,
        effectiveTargetApplicationVersion
      );
      if (blocker !== undefined) {
        return this.resetBlockedRelocation(blockedRelocation(
          mode,
          effectiveTargetApplicationVersion,
          blocker
        ));
      }
      const descriptorPreparation = await this.routeMeshCoordinator.prepareHostRetire(deadlineMs);
      if (descriptorPreparation !== 'prepared') {
        return this.resetBlockedRelocation(blockedRelocation(
          mode,
          effectiveTargetApplicationVersion,
          descriptorPreparation === 'deadline_exceeded'
            ? ZLinkFrameworkRelocationReason.DeadlineExceeded
            : ZLinkFrameworkRelocationReason.StoreUnavailable
        ));
      }
      this.setRuntimeState(ZLinkFrameworkRuntimeState.Relocating);
      const drained = await this.routeMeshCoordinator.drainHost(deadlineMs);
      if (drained.kind === 'forceStopped') {
        return this.completeRelocation(blockedRelocation(
          mode,
          effectiveTargetApplicationVersion,
          relocationReason(drained.reason)
        ));
      }
      await this.publishHostRelocated();
      return this.completeRelocation({
        mode,
        effectiveTargetApplicationVersion,
        outcome: ZLinkFrameworkRelocationOutcome.Relocated,
        reason: ZLinkFrameworkRelocationReason.None
      });
    } catch (error) {
      return this.completeRelocation(blockedRelocation(
        mode,
        effectiveTargetApplicationVersion,
        error instanceof Error && /deadline/i.test(error.message)
          ? ZLinkFrameworkRelocationReason.DeadlineExceeded
          : ZLinkFrameworkRelocationReason.RelocationFailed
      ));
    }
  }

  private async runShutdown(_deadlineMs: number): Promise<ZLinkFrameworkTerminationResult> {
    try {
      if (this.relocationOperation !== undefined
        && this.runtimeState === ZLinkFrameworkRuntimeState.Relocating) {
        await this.relocationOperation;
      }
      this.setRuntimeState(ZLinkFrameworkRuntimeState.Draining);
      await this.stop();
      return this.completeTermination({
        outcome: ZLinkFrameworkTerminationOutcome.Stopped,
        reason: ZLinkFrameworkTerminationReason.None
      });
    } catch (error) {
      await this.stop().catch(() => undefined);
      return this.completeTermination({
        outcome: ZLinkFrameworkTerminationOutcome.ForceStopped,
        reason: error instanceof Error && /deadline/i.test(error.message)
          ? ZLinkFrameworkTerminationReason.DeadlineExceeded
          : ZLinkFrameworkTerminationReason.TeardownFailed
      });
    }
  }

  private resetBlockedRelocation(
    result: ZLinkFrameworkRelocationResult
  ): ZLinkFrameworkRelocationResult {
    this.runtimeDeadline = undefined;
    this.relocationTargetApplicationVersion = undefined;
    this.relocationOperation = undefined;
    return result;
  }

  private completeRelocation(
    result: ZLinkFrameworkRelocationResult
  ): ZLinkFrameworkRelocationResult {
    this.runtimeRelocationResult = result;
    this.runtimeDeadline = undefined;
    this.setRuntimeState(result.outcome === ZLinkFrameworkRelocationOutcome.Relocated
      ? ZLinkFrameworkRuntimeState.Relocated
      : ZLinkFrameworkRuntimeState.Error);
    return result;
  }

  private completeTermination(
    result: ZLinkFrameworkTerminationResult
  ): ZLinkFrameworkTerminationResult {
    this.runtimeTerminationResult = result;
    this.runtimeDeadline = undefined;
    this.setRuntimeState(ZLinkFrameworkRuntimeState.Stopped);
    return result;
  }

  private setRuntimeState(state: ZLinkFrameworkRuntimeState): void {
    if (this.runtimeState === state) return;
    this.runtimeState = state;
    this.runtimeSequence += 1n;
    const status = this.status;
    for (const observer of this.runtimeObservers) observer.push(status);
    if (state === ZLinkFrameworkRuntimeState.Stopped) {
      for (const observer of this.runtimeObservers) observer.return();
      this.runtimeObservers.clear();
    }
  }

  private publishActorHandoffEvent(event: {
    readonly marker: string;
    readonly actorId: string;
    readonly index?: number;
    readonly requestSeq?: string;
    readonly flags?: number;
  }): void {
    void this.runtimeEventPublisher.publish({
      sourceName: 'zlink.framework.actor-handoff',
      timestamp: new Date(),
      ...event
    });
  }

  get locationRuntimeQuery(): ZLinkLocationRuntimeQuery | undefined {
    return this.ensureLocationRuntime();
  }

  get eventPublisher(): ZLinkRuntimeEventPublisher {
    return this.runtimeEventPublisher;
  }

  /**
   * Runtime toggle (ZLinkMessageFlowControl): flip the shared live-mode cell so every
   * surface starts/stops tracing without a restart.
   */
  setMessageFlowMode(mode: ZLinkMessageFlowLogMode): void {
    this.messageFlowModeCell.mode = mode;
  }

  messageFlowMode(): ZLinkMessageFlowLogMode {
    return this.messageFlowModeCell.mode;
  }

  get context(): ZLinkBackendContext | undefined {
    return this.executionState?.context as ZLinkBackendContext | undefined;
  }

  get taskRunner(): ZLinkFrameworkExecutionState['taskRunner'] | undefined {
    return this.executionState?.taskRunner;
  }

  get errorSink(): ZLinkFrameworkExecutionState['errorSink'] | undefined {
    return this.executionState?.errorSink;
  }

  async start(): Promise<void> {
    await this.runLifecycle(() => this.startCore());
  }

  private async startCore(): Promise<void> {
    if (this.executionState !== undefined) {
      return;
    }

    this.setRuntimeState(ZLinkFrameworkRuntimeState.Preparing);
    this.lifecycleSink?.push('framework:start');
    const channelAdapter = this.backendAdapterFactory.createChannelAdapter();
    const context = channelAdapter.createContext();
    let channelRuntime: ZLinkChannelRuntimeManager | undefined;
    let spotNodeRuntime: ZLinkSpotNodeRuntimeManager | undefined;
    let streamRuntime: ZLinkStreamRuntimeManager | undefined;
    let startedLocationRuntime: ZLinkLocationRuntime | undefined;
    let statefulAuthorityRoutes: ZLinkStatefulAuthorityRouteRuntime | undefined;
    try {
      this.executionState = new ZLinkFrameworkExecutionState(context);
      // Seed the shared live-mode cell from the configured mode (default errorsOnly).
      this.messageFlowModeCell.mode =
        this.options.registration.dispatch?.diagnostics.messageFlow ??
        ZLinkMessageFlowLogMode.ErrorsOnly;
      const dispatchErrors = this.createDispatchErrorReporter(this.executionState.errorSink);
      channelRuntime = new ZLinkChannelRuntimeManager(
        this.options.registration,
        channelAdapter,
        context,
        this.options.providerResolver,
        this.createChannelRuntimeOptions()
      );
      this.channelRuntime = channelRuntime;
      channelRuntime.prepareMeshDispatch(this.executionState.taskRunner);
      spotNodeRuntime = new ZLinkSpotNodeRuntimeManager(
        this.createSpotNodeRuntimeOptions(context, dispatchErrors)
      );
      await spotNodeRuntime.start();
      this.registerUserSpotHandlers?.(spotNodeRuntime);
      channelRuntime.bindRouteMeshRouters();
      // Start bound receivers before publishing Serving descriptors. A
      // discovered ClientServer endpoint must already be able to dispatch.
      this.executionState.listenerTasks.push(...channelRuntime.start(this.executionState.taskRunner));
      const locationRuntime = await this.locationOwner.startForRuntime(
        this.meshRouters.primaryMeshName(),
        spotNodeRuntime,
        channelRuntime
      );
      startedLocationRuntime = locationRuntime;
      const locationStore = this.locationOwner.currentStores?.locationStore;
      if (locationStore !== undefined) {
        await spotNodeRuntime.publishMeshNodeState(
          ZLinkFrameworkRuntimeState.Preparing,
          this.executionState.abortController.signal
        );
        for (const [meshName, node] of spotNodeRuntime.meshNodesByName) {
          const activationNode = node as typeof node & {
            registerAsyncInstanceActivationAuthority?: (
              authority: ServiceAsyncInstanceActivationAuthority
            ) => void;
            registerInstanceApplicationLifecycle?: (
              lifecycle: import('../foundation/service-stateful-runtime')
                .ServiceInstanceApplicationLifecycle
            ) => void;
          };
          const spotManager = this.spotManager;
          if (spotManager !== undefined) {
            activationNode.registerInstanceApplicationLifecycle?.({
              materialize: (target, objectGeneration) =>
                spotManager.materializeInstance(
                  meshName,
                  target.stableType,
                  target.targetSpotId as never,
                  objectGeneration,
                  this.executionState?.abortController.signal
                ),
              discard: (target) =>
                spotManager.discardInstance(meshName, target.targetSpotId as never)
            });
          }
          activationNode.registerAsyncInstanceActivationAuthority?.(
            new ZLinkInstanceActivationAuthority({
              store: locationStore,
              relocationStore:
                this.options.registration.locations.relocationStoreInstance,
              meshName,
              owner: () => this.locationOwner.currentRuntime?.currentOwnerToken
            })
          );
        }
        statefulAuthorityRoutes = new ZLinkStatefulAuthorityRouteRuntime({
          store: locationStore,
          creationStore: locationStore,
          relocationStore:
            this.options.registration.locations.relocationStoreInstance,
          meshNodes: spotNodeRuntime.meshNodesByName,
          pollingIntervalMs:
            this.options.registration.locations.options.pollingIntervalMs
            ?? zlinkDefaultLocationOptions.pollingIntervalMs,
          pageSize: 1000,
          reportError: (error) =>
            this.runtimeOrPreStartErrorSink.reportRuntimeTaskException(
              'stateful authority route reconciliation',
              error
            )
        });
        await statefulAuthorityRoutes.start(this.executionState.abortController.signal);
        this.statefulAuthorityRoutes = statefulAuthorityRoutes;
      }
      this.spotNodeRuntime = spotNodeRuntime;
      streamRuntime = new ZLinkStreamRuntimeManager({
        registration: this.options.registration,
        backendAdapterFactory: this.backendAdapterFactory,
        context,
        bindingRuntime: this.streamBindingRuntime,
        providerResolver: this.options.providerResolver,
        dispatchErrors,
        metrics: this.metrics,
        acceptNewSession: (meshName) => meshName === undefined
          ? this.admission.acceptsNewWork
          : this.admission.accepts(meshName),
        primaryMeshName: this.meshRouters.primaryMeshName(),
        claimApplicationWork: (meshName) =>
          this.admission.claim(meshName, 'STREAM session dispatch'),
        nativeMeshNode: spotNodeRuntime.primaryMeshNode,
        meshCompletions: spotNodeRuntime.primaryMeshCompletions,
        nativeMeshNodeForName: (meshName) => spotNodeRuntime?.meshNode(meshName),
        meshCompletionsForName: (meshName) => spotNodeRuntime?.meshCompletionTable(meshName)
      });
      streamRuntime.start();
      this.streamRuntime = streamRuntime;
      if (this.options.registration.monitoring !== undefined) {
        this.monitoringRuntime = new ZLinkMonitoringRuntime({
          registration: this.options.registration,
          channelRuntime,
          locationRuntime,
          monitoringAdapter: this.backendAdapterFactory.createMonitoringAdapter(),
          publisher: this.runtimeEventPublisher
        });
        this.monitoringRuntime.start(this.executionState);
      }
      await spotNodeRuntime.publishMeshNodeState(
        ZLinkFrameworkRuntimeState.Serving,
        this.executionState.abortController.signal
      );
      this.routeMeshCoordinator.markServing();
      this.setRuntimeState(ZLinkFrameworkRuntimeState.Serving);
      this.lifecycleSink?.push('framework:started');
    } catch (error) {
      await statefulAuthorityRoutes?.stop();
      await rollbackRuntimeStart({
        context,
        startedLocationRuntime,
        monitoringRuntime: this.monitoringRuntime,
        streamRuntime,
        spotNodeRuntime,
        channelRuntime
      });
      this.executionState = undefined;
      this.channelRuntime = undefined;
      this.spotNodeRuntime = undefined;
      this.streamRuntime = undefined;
      this.monitoringRuntime = undefined;
      this.statefulAuthorityRoutes = undefined;
      this.locationOwner.clearForStop();
      this.cachedLocationSpotRouteResolver = undefined;
      throw error;
    }
  }

  async stop(): Promise<void> {
    await this.runLifecycle(() => this.stopCore());
  }

  private async stopCore(): Promise<void> {
    const state = this.executionState;
    if (state === undefined) {
      return;
    }

    const channelRuntime = this.channelRuntime;
    const spotNodeRuntime = this.spotNodeRuntime;
    const streamRuntime = this.streamRuntime;
    const monitoringRuntime = this.monitoringRuntime;
    const statefulAuthorityRoutes = this.statefulAuthorityRoutes;
    const locationSnapshot = this.locationOwner.clearForStop();
    this.executionState = undefined;
    this.channelRuntime = undefined;
    this.spotNodeRuntime = undefined;
    this.streamRuntime = undefined;
    this.monitoringRuntime = undefined;
    this.statefulAuthorityRoutes = undefined;
    this.cachedLocationSpotRouteResolver = undefined;
    this.lifecycleSink?.push('framework:stop');
    await statefulAuthorityRoutes?.stop();
    await stopRuntimeParts({
      state,
      locationSnapshot,
      monitoringRuntime,
      streamRuntime,
      spotNodeRuntime,
      channelRuntime,
      serviceRelocation: this.serviceRelocation
    });
    this.meshSubmitters.dispose();
    this.lifecycleSink?.push('framework:stopped');
    if (this.shutdownOperation === undefined) {
      this.setRuntimeState(ZLinkFrameworkRuntimeState.Stopped);
    }
  }

  async onApplicationBootstrap(): Promise<void> {
    await this.start();
  }

  async onApplicationShutdown(): Promise<void> {
    const meshNames = [...this.options.registration.spotNodes.keys()];
    if (meshNames.length > 0) {
      await this.routeMeshCoordinator.drainHost();
    }
    await this.stop();
  }

  private async performMeshDrain(meshName: string, signal: AbortSignal): Promise<void> {
    await this.serviceRelocation.relocateMesh(
      meshName,
      this.relocationTargetApplicationVersion,
      signal
    );
    const handedOffActorIds = new Set<string>();
    while (!await this.handoffActorsForDrain(meshName, handedOffActorIds, signal)) {
      await waitForDrainRetry(
        this.options.registration.locations.options.pollingIntervalMs
          ?? zlinkDefaultLocationOptions.pollingIntervalMs,
        signal
      );
    }
    await this.streamRuntime?.notifyServerDrain(meshName);
    await this.drainSpots(meshName, signal);
  }

  private async publishMeshRetiring(
    meshName: string,
    signal?: AbortSignal
  ): Promise<void> {
    await this.spotNodeRuntime?.publishMeshNodeState(
      ZLinkFrameworkRuntimeState.Relocating,
      signal,
      meshName
    );
  }

  private async publishMeshServing(
    meshName: string,
    signal?: AbortSignal
  ): Promise<void> {
    await this.spotNodeRuntime?.reconcileAndPublishMeshNodeState(
      ZLinkFrameworkRuntimeState.Serving,
      meshName,
      signal
    );
  }

  private async publishMeshDraining(
    meshName: string,
    signal?: AbortSignal
  ): Promise<void> {
    const node = this.spotNodeRuntime?.meshNode(meshName);
    for (const channelName of Object.keys(
      this.options.registration.spotNodes.get(meshName)?.meshChannels ?? {}
    )) {
      node?.setChannelWeight(channelName, 0);
    }
    await this.spotNodeRuntime?.publishMeshNodeState(
      ZLinkFrameworkRuntimeState.Draining,
      signal,
      meshName
    );
  }

  private async preflightAutomaticPeerReadiness(
    deadlineMs: number,
    targetApplicationVersion: bigint
  ): Promise<ZLinkFrameworkRelocationReason | undefined> {
    if (this.options.registration.spotNodes.size === 0) return undefined;
    const location = this.locationOwner.currentRuntime;
    if (location === undefined) return ZLinkFrameworkRelocationReason.StoreUnavailable;

    const deadlineAt = Date.now() + deadlineMs;
    let storeUnavailable = false;
    while (Date.now() < deadlineAt) {
      try {
        if (await this.hasExactAutomaticPeerReadiness(
          location,
          targetApplicationVersion
        )) return undefined;
        storeUnavailable = false;
      } catch {
        storeUnavailable = true;
      }
      await new Promise((resolve) => setTimeout(
        resolve,
        Math.min(
          this.options.registration.locations.options.pollingIntervalMs
            ?? zlinkDefaultLocationOptions.pollingIntervalMs,
          Math.max(1, deadlineAt - Date.now())
        )
      ));
    }
    return storeUnavailable
      ? ZLinkFrameworkRelocationReason.StoreUnavailable
      : ZLinkFrameworkRelocationReason.TargetUnavailable;
  }

  private async hasExactAutomaticPeerReadiness(
    location: Pick<ZLinkLocationRuntime, 'listLiveMeshNodes'>,
    targetApplicationVersion: bigint
  ): Promise<boolean> {
    for (const [meshName, registration] of this.options.registration.spotNodes) {
      if ((registration.router?.manualConnections?.length ?? 0) > 0
        || (registration.router?.manualPeerConnections?.length ?? 0) > 0) {
        continue;
      }
      const node = this.spotNodeRuntime?.meshNode(meshName);
      if (node === undefined) return false;
      const local = node.status();
      const localDescriptor = this.spotNodeRuntime?.meshNodeDescriptor(meshName);
      if (localDescriptor === undefined) return false;
      const descriptors = await location.listLiveMeshNodes(meshName);
      const peers = node.peers();
      if (!hasExactPeerReadiness(descriptors, {
        ...local,
        applicationVersion: targetApplicationVersion,
        maintenanceWave: localDescriptor.maintenanceWave
      }, peers)) return false;
    }
    return true;
  }

  private async publishHostRelocated(): Promise<void> {
    await Promise.all([...this.options.registration.spotNodes.keys()].map(meshName =>
      this.spotNodeRuntime?.publishMeshNodeState(
        ZLinkFrameworkRuntimeState.Relocated,
        undefined,
        meshName
      )));
  }

  private async publishHostDraining(signal: AbortSignal): Promise<void> {
    try {
      const runtime = this.locationOwner.currentRuntime;
      while (runtime !== undefined && !await runtime.publishDraining(signal)) {
        await waitForDrainRetry(
          this.options.registration.locations.options.pollingIntervalMs
            ?? zlinkDefaultLocationOptions.pollingIntervalMs,
          signal
        );
      }
    } catch (error) {
      throw new ZLinkDrainingStatePublishError(error);
    }
    if (this.locationOwner.currentRuntime !== undefined) {
      await waitForDrainRetry(
        this.options.registration.locations.options.pollingIntervalMs
          ?? zlinkDefaultLocationOptions.pollingIntervalMs,
        signal
      );
    }
  }

  private async cleanupOwnerForDrain(signal: AbortSignal): Promise<void> {
    const runtime = this.locationOwner.currentRuntime;
    if (runtime === undefined) return;
    for (;;) {
      try {
        await runtime.cleanupOwner(signal);
        return;
      } catch {
        try {
          await waitForDrainRetry(
            this.options.registration.locations.options.pollingIntervalMs
              ?? zlinkDefaultLocationOptions.pollingIntervalMs,
            signal
          );
        } catch (error) {
          throw new ZLinkOwnerCleanupError(error);
        }
      }
    }
  }

  private async forceStopMesh(meshName: string): Promise<void> {
    await waitForForcedSessionNotification(this.streamRuntime?.notifyServerDrain(meshName));
    await this.spotManager?.drainForShutdown(meshName);
  }

  private async drainSpots(meshName: string, signal?: AbortSignal): Promise<void> {
    const manager = this.spotManager;
    if (manager === undefined) return;
    await manager.drainForShutdown(meshName, signal);
  }

  private async handoffActorsForDrain(
    drainingMeshName: string,
    handedOffActorIds: Set<string>,
    signal?: AbortSignal
  ): Promise<boolean> {
    const actorManager = this.actorManager;
    if (actorManager === undefined) return true;
    let allMoved = true;
    for (const state of actorManager.snapshotStates()) {
      const actor = state.actor;
      const sourceNodeRid = state.nativeActorRef?.nodeRid;
      const actorType = state.actorType;
      if (actor === undefined || actorType === undefined || sourceNodeRid === undefined) continue;
      if (handedOffActorIds.has(actor.context.actorId)) continue;
      if (state.isMoving) {
        allMoved = false;
        continue;
      }
      const meshName = this.meshRouters.actorMeshName(actorType);
      if (meshName !== drainingMeshName) continue;
      try {
        const resolver = this.locationOwner.createRefResolver([meshName]);
        const excludedTargets = new Set<string>();
        let moved = false;
        for (;;) {
          const target = await resolver?.selectActorPlacement(
            meshName,
            actorType,
            sourceNodeRid,
            signal,
            excludedTargets
          );
          if (target === undefined) break;
          try {
            const context = actor.context as typeof actor.context & {
              [ZLINK_ACTOR_JOIN_ENTRY_SPOT_RUNTIME](
                nodeRid: RoutingId | undefined,
                request: unknown,
                signal?: AbortSignal
              ): Promise<boolean>;
            };
            if (!await context[ZLINK_ACTOR_JOIN_ENTRY_SPOT_RUNTIME](target, undefined, signal)) break;
            moved = true;
            break;
          } catch (error) {
            if (!(error instanceof ZLinkFrameworkException)
              || error.kind !== ZLinkFrameworkErrorKind.PlacementCapacityExhausted) {
              throw error;
            }
            excludedTargets.add(String(target));
          }
        }
        if (!moved) {
          allMoved = false;
          continue;
        }
        handedOffActorIds.add(actor.context.actorId);
        this.metrics.count('zlink.drain.actors.handed_off');
      } catch (error) {
        this.runtimeOrPreStartErrorSink.reportRuntimeTaskException('drain actor handoff', error);
        allMoved = false;
      }
    }
    return allMoved;
  }

  setActorManager(actorManager: DefaultZLinkActorManager): void {
    this.actorManager = actorManager;
  }

  setSpotManager(spotManager: DefaultZLinkSpotManager): void {
    this.spotManager = spotManager;
    for (const [meshName, node] of this.spotNodeRuntime?.meshNodesByName ?? []) {
      const activationNode = node as typeof node & {
        registerInstanceApplicationLifecycle?: (
          lifecycle: import('../foundation/service-stateful-runtime')
            .ServiceInstanceApplicationLifecycle
        ) => void;
      };
      activationNode.registerInstanceApplicationLifecycle?.({
        materialize: (target, objectGeneration) =>
          spotManager.materializeInstance(
            meshName,
            target.stableType,
            target.targetSpotId as never,
            objectGeneration,
            this.executionState?.abortController.signal
          ),
        discard: (target) =>
          spotManager.discardInstance(meshName, target.targetSpotId as never)
      });
    }
  }

  createActorManagerOptions(spotRouteResolver?: ZLinkSpotRouteResolver): Pick<
    ZLinkActorManagerOptions,
    | 'joinCoordinator'
    | 'actorMeshNameProvider'
    | 'actorLeaveSpot'
    | 'messageSerializers'
    | 'nativeActorNode'
    | 'nativeActorNodeProvider'
    | 'actorCreatedNodeRidProvider'
    | 'actorRefResolver'
    | 'actorCreatedNotifier'
    | 'actorDestroyedCleanup'
    | 'locationLifecycle'
    | 'boundSessionFactory'
    | 'shutdownSignal'
    | 'admission'
  > {
    this.ensureLocationRuntime();
    return this.actorRuntimeOptionsFactory().createActorManagerOptions(spotRouteResolver);
  }

  createActorClientOptions(): ConstructorParameters<typeof DefaultZLinkActorClient>[0] {
    this.ensureLocationRuntime();
    return this.actorRuntimeOptionsFactory().createActorClientOptions();
  }

  createLocationHandleResolver(): ZLinkStoreLocationResolvers | undefined {
    this.ensureLocationRuntime();
    return this.locationOwner.createRefResolver(this.meshRouters.spotLocationMeshNames());
  }

  createSpotManagerOptions(): Partial<ZLinkSpotManagerOptions> {
    this.ensureLocationRuntime();
    return new ZLinkSpotRuntimeOptionsFactory({
      registration: this.options.registration,
      channelTransport: this.channelTransport,
      routeTransport: this.routeTransport,
      addressTransport: this.spotAddressTransport,
      spotPublisherTransport: this.spotPublisherTransport,
      meshRouters: this.meshRouters,
      runtimeEventPublisher: this.runtimeEventPublisher,
      spotNodeRuntime: () => this.spotNodeRuntime,
      actorManager: () => this.actorManager,
      // User Spot visibility is published only by the generic authority
      // coordinator after application initialization. The legacy location
      // claim would expose a Creating object before that Ready barrier.
      locationLifecycle: () => undefined,
      createLocationSpotRouteResolver: () => this.createLocationSpotRouteResolver(),
      boundSessionRelay: this.boundSessionRelay,
      actorHandoff: this.actorHandoff,
      dispatchErrorReporter: (errorSink) => this.createDispatchErrorReporter(errorSink),
      runtimeOrPreStartErrorSink: this.runtimeOrPreStartErrorSink,
      detachedTaskRunner: this.detachedTaskRunner(),
      metrics: this.metrics,
      admission: this.admission
    }).create(this.actorTransferRuntime);
  }

  createPublicSpotManager(local: DefaultZLinkSpotManager): import('../../contracts').ZLinkSpotManager {
    const locationStore = this.locationOwner.locationStore();
    if (locationStore === undefined) {
      throw new ZLinkConfigurationException(
        'User Spot creation requires a Location Store.'
      );
    }
    const coordinator = new ZLinkUserSpotCreationCoordinator({
      store: locationStore,
      remoteCreate: (meshName, targetNodeRid, request, timeoutMs) => {
        const node = this.spotNodeRuntime?.meshNode(meshName);
        if (node === undefined) {
          throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ObjectClientNotConfigured,
            `RouteMesh '${meshName}' has no local client node.`
          );
        }
        return node.requestUserSpotCreate(targetNodeRid, request, timeoutMs);
      },
      decodeRemoteReply: payload => {
        const message = BindingMessage.from(payload);
        try {
          return decodeFrameworkPayloadMessage(
            message,
            this.options.registration.messageSerializers
          );
        } finally {
          message.close();
        }
      },
      target: async (request, signal, excludedNodeRids) => {
        const clientMeshes = [...this.options.registration.spotNodes]
          .filter(([, node]) => hasObjectClientCapability(node.objectRole))
          .map(([meshName]) => meshName);
        const meshes = request.meshName === undefined ? clientMeshes : [request.meshName];
        if (meshes.length === 0) {
          throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ObjectClientNotConfigured,
            'No object-client RouteMesh is configured.'
          );
        }
        if (request.meshName === undefined && meshes.length > 1) {
          throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.MeshSelectionRequired,
            'Multiple object-client RouteMeshes are configured; call inMesh(...).'
          );
        }
        const meshName = meshes[0]!;
        if (!this.options.registration.spotNodes.has(meshName)) {
          throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.MeshNotFound,
            `RouteMesh '${meshName}' is not configured.`
          );
        }
        if (!clientMeshes.includes(meshName)) {
          throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ObjectClientNotConfigured,
            `RouteMesh '${meshName}' has no object-client role.`
          );
        }
        const descriptors = (await locationStore.listMeshNodes(meshName, undefined, signal)).items
          .filter(descriptor => {
            const capability = descriptor.objectCapabilities.find(candidate =>
              candidate.objectKind === 'user_spot'
              && candidate.stableType === request.stableType);
            const spotTypeCapacity = descriptor.populationCapacity.spotTypes.find(candidate =>
              candidate.objectKind === 'user_spot'
              && candidate.stableType === request.stableType);
            const spots = descriptor.populationCapacity.spots;
            return descriptor.state === 1
              && descriptor.objectRole === 'server'
              && descriptor.placementWeight > 0
              && excludedNodeRids?.has(String(descriptor.rid)) !== true
              && spots.active + spots.reserved < spots.limit
              && capability !== undefined
              && (spotTypeCapacity === undefined
                || spotTypeCapacity.limit === 0
                || spotTypeCapacity.active + spotTypeCapacity.reserved
                  < spotTypeCapacity.limit);
          });
        const selected = selectLocationPlacementDescriptor(descriptors);
        if (selected === undefined) return undefined;
        const localStatus = this.spotNodeRuntime?.meshNode(meshName)?.status();
        return {
          meshName,
          nodeRid: selected.rid,
          nodeGeneration: selected.lifecycleGeneration,
          owner: {
            ownerId: selected.ownerId,
            leaseGeneration: selected.leaseGeneration
          },
          isLocal: localStatus !== undefined
            && String(localStatus.routingId) === String(selected.rid)
            && localStatus.lifecycleGeneration === selected.lifecycleGeneration
        };
      }
    });
    const factories = new Map(
      [...this.options.registration.spotNodes].map(([meshName, node]) => [
        meshName,
        new Map(Object.entries(node.spotFactoryRegistrations ?? {}))
      ])
    );
    const registerUserSpotHandlers = (runtime: ZLinkSpotNodeRuntimeManager) => {
      for (const [meshName] of this.options.registration.spotNodes) {
        const node = runtime.meshNode(meshName);
        if (node === undefined) continue;
        node.registerUserSpotOperationHandler({
        create: async (record, signal) => {
          const coordinated = await coordinator.handleRemoteCreate(
            record,
            async (requestPayload, authority, createSignal) => {
              const selected = factories.get(meshName)?.get(record.stableType);
              if (selected === undefined) {
                throw new ZLinkConfigurationException(
                  `User Spot factory '${record.stableType}' is not registered on RouteMesh '${meshName}'.`
                );
              }
              const message = BindingMessage.from(requestPayload);
              let request: unknown;
              try {
                request = decodeFrameworkPayloadMessage(
                  message,
                  this.options.registration.messageSerializers
                );
              } finally {
                message.close();
              }
              local.beginUserSpotPublication(meshName, record.spotId as never);
              try {
                const result = await local.getOrCreateWithAuthority(
                  meshName,
                  selected.implementation as never,
                  record.spotId as never,
                  request,
                  {
                    stableType: record.stableType,
                    objectGeneration: authority.objectGeneration,
                    authorityOwnerGeneration: authority.authorityOwnerGeneration
                  },
                  createSignal
                );
                return {
                  ...result,
                  publication: {
                    publish: () => local.publishUserSpot(meshName, record.spotId as never),
                    abort: () => local.abortUserSpotPublication(meshName, record.spotId as never)
                  }
                };
              } catch (error) {
                local.abortUserSpotPublication(meshName, record.spotId as never);
                throw error;
              }
            },
            async cleanupSignal => {
              await local.close(meshName, record.spotId as never, cleanupSignal);
            },
            signal
          );
          const reply = coordinated.result.reply;
          let payload;
          if (
            reply !== undefined
            && coordinated.result.state !== ZLinkSpotCreateState.Existing
          ) {
            const message = encodeFrameworkPayloadMessage(
              reply,
              this.options.registration.messageSerializers
            );
            try {
              payload = {
                packetName: 'ZLinkFrameworkUserSpotReply',
                contentType: 'application/octet-stream',
                payload: Buffer.from(message.data())
              };
            } finally {
              message.close();
            }
          }
          return {
            terminalResult: RequestResult.Ok,
            failureCode: 0,
            tail: {
              kind: 'userSpotCreate' as const,
              createResult: coordinated.result.state === ZLinkSpotCreateState.Existing
                ? 'existing' as const
                : coordinated.result.state === ZLinkSpotCreateState.Created
                  ? 'created' as const
                  : 'rejected' as const,
              spotId: String(coordinated.spot.spotId),
              objectGeneration: coordinated.spot.objectGeneration
            },
            ...(payload === undefined ? {} : { payload })
          };
        },
        close: async (record, signal) => {
          if (!local.hasActiveSpot(record.target.spotId as never)) {
            throw new ZLinkFrameworkException(
              ZLinkFrameworkErrorKind.SpotMoving,
              `User Spot '${record.target.spotId}' is not materialized on its authority owner.`,
              true
            );
          }
          if (!local.canCloseUserSpot(meshName, record.target.spotId as never)) {
            return {
              terminalResult: RequestResult.Ok,
              failureCode: 0,
              tail: {
                kind: 'userSpotClose' as const,
                closed: false
              }
            };
          }
          return {
            terminalResult: RequestResult.Ok,
            failureCode: 0,
            tail: {
              kind: 'userSpotClose' as const,
              closed: await coordinator.handleRemoteClose(
                record,
                (spot, closeSignal) => local.close(
                  spot.meshName,
                  spot.spotId,
                  closeSignal
                ),
                signal
              )
            }
          };
        }
        });
      }
    };
    this.registerUserSpotHandlers = registerUserSpotHandlers;
    if (this.spotNodeRuntime !== undefined) {
      registerUserSpotHandlers(this.spotNodeRuntime);
    }
    return new ZLinkPublicSpotManager({
      local,
      coordinator,
      factories,
      resolver: () => this.createLocationSpotRouteResolver(),
      isLocalNode: (meshName, nodeRid) => {
        const status = this.spotNodeRuntime?.meshNode(meshName)?.status();
        return status !== undefined && String(status.routingId) === String(nodeRid);
      },
      remoteClose: (meshName, targetNodeRid, request, timeoutMs) => {
        const node = this.spotNodeRuntime?.meshNode(meshName);
        if (node === undefined) {
          throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ObjectClientNotConfigured,
            `RouteMesh '${meshName}' has no local client node.`
          );
        }
        return node.requestUserSpotClose(targetNodeRid, request, timeoutMs);
      },
      defaultTimeoutMs: this.options.registration.requestTimeoutMs ?? 30_000,
      messageSerializers: this.options.registration.messageSerializers
    });
  }

  private actorRuntimeOptionsFactory(): ZLinkActorRuntimeOptionsFactory {
    return new ZLinkActorRuntimeOptionsFactory({
      registration: this.options.registration,
      routeTransport: this.routeTransport,
      streamBindingRuntime: this.streamBindingRuntime,
      providerResolver: this.options.providerResolver,
      spotManager: () => this.spotManager,
      actorManager: () => this.actorManager,
      primaryMeshNode: () => this.requirePrimaryMeshNode(),
      primaryMeshNodeOrUndefined: () => this.spotNodeRuntime?.primaryMeshNode,
      primaryMeshCompletions: () => this.spotNodeRuntime?.primaryMeshCompletions,
      meshNode: (meshName) => this.spotNodeRuntime?.meshNode(meshName),
      meshCompletions: (meshName) => this.spotNodeRuntime?.meshCompletionTable(meshName),
      notifyEntrySpotActorCreated: (nodeRid, actor, createRequest, signal) =>
        this.spotNodeRuntime?.notifyEntrySpotActorCreated(nodeRid, actor, createRequest, signal)
          ?? Promise.resolve(undefined),
      locationLifecycle: () => this.locationOwner.currentLifecycle,
      primaryMeshName: () => this.meshRouters.primaryMeshName(),
      actorMeshName: (actorType) => this.meshRouters.actorMeshName(actorType),
      createLocationSpotRouteResolver: () => this.createLocationSpotRouteResolver(),
      createActorLocationResolver: () => this.createActorLocationResolver(),
      forgetDestroyedActorRef: (actorId) => this.destroyedActorRefs.delete(actorId),
      rememberDestroyedActorRef: (actorId, actorRef) => this.destroyedActorRefs.set(actorId, actorRef),
      publishActorAuthority: async (actorType, actorRef, ownerNodeGeneration, signal) => {
        const store = this.locationOwner.currentStores?.locationStore;
        if (store === undefined) return;
        const owner = this.locationOwner.currentRuntime?.currentOwnerToken;
        const meshName = this.meshRouters.actorMeshName(actorType);
        if (owner === undefined || meshName === undefined) {
          throw new ZLinkConfigurationException(
            `Actor '${actorRef.actorId}' authority requires an active owner and Actor RouteMesh.`
          );
        }
        await publishInitialActorAuthority(store, {
          actorType,
          actor: actorRef,
          meshName,
          ownerNodeGeneration,
          owner
        }, signal);
      },
      reportPostCommitError: (error) =>
        this.runtimeOrPreStartErrorSink.reportRuntimeTaskException('post-commit actor binding', error),
      reportBoundSessionSendError: (error) =>
        this.runtimeOrPreStartErrorSink.reportRuntimeTaskException('bound session one-way submit', error),
      actorHandoff: this.actorHandoff,
      actorTransferRuntime: this.actorTransferRuntime,
      actorTransferRegistry: this.actorTransferRegistry,
      shutdownSignal: () => this.executionState?.abortController.signal,
      metrics: this.metrics,
      admission: this.admission,
      actorPacketTargetForState: (actorId) =>
        this.boundSessionRelay.actorPackets.actorPacketTargetForState(actorId),
      meshSubmitters: this.meshSubmitters,
      flowCreationEnabled: () => this.flowCreationEnabled(),
      traceBoundSessionSend: (actorId, packetName) => {
        const flow = this.createDispatchErrorReporter(this.runtimeOrPreStartErrorSink).flow;
        flowIfEnabled(flow, ZLinkMessageFlowOutcome.Sent)?.trace({
          outcome: ZLinkMessageFlowOutcome.Sent,
          surface: ZLinkDispatchErrorSurface.SpotActor,
          messageKind: ZLinkDispatchMessageKind.ActorSend,
          packetName,
          actorId
        });
      }
    });
  }

  private createChannelRuntimeOptions() {
    return new ZLinkChannelRuntimeOptionsFactory({
      monitoringAdapter: this.backendAdapterFactory.createMonitoringAdapter(),
      messageFlowModeCell: this.messageFlowModeCell,
      boundSessionRelay: this.boundSessionRelay,
      spotManager: () => this.spotManager,
      oneWayFailureSink: (error) =>
        this.runtimeOrPreStartErrorSink.reportRuntimeTaskException('channel one-way submit', error)
    }).create();
  }

  private createSpotNodeRuntimeOptions(
    context: ZLinkBackendContext,
    dispatchErrors: ZLinkDispatchErrorReporter
  ) {
    const options = new ZLinkSpotNodeRuntimeOptionsFactory({
      registration: this.options.registration,
      backendAdapterFactory: this.backendAdapterFactory,
      context,
      channelTransport: this.channelTransport,
      routeTransport: this.routeTransport,
      spotPublisherTransport: this.spotPublisherTransport,
      meshRouters: this.meshRouters,
      providerResolver: this.options.providerResolver,
      dispatchErrors,
      runtimeEventPublisher: this.runtimeEventPublisher,
      entryActorRuntime: this.entryActorRuntime,
      actorTransferRuntime: this.actorTransferRuntime,
      boundSessionRelay: this.boundSessionRelay,
      actorHandoff: this.actorHandoff,
      detachedTaskRunner: this.detachedTaskRunner(),
      metrics: this.metrics
    }).create();
    return {
      ...options,
      meshRecordDispatcher: (meshName: string, owner: ReadyRecord, record: ReceiveRecord) =>
        this.dispatchMeshRecord(meshName, owner, record, this.executionState?.abortController.signal)
    };
  }

  private async dispatchMeshRecord(
    meshName: string,
    owner: ReadyRecord,
    record: ReceiveRecord,
    signal?: AbortSignal
  ): Promise<void> {
    if (record.operationKind === OperationKind.ActorJoin) {
      if (this.spotManager === undefined) {
        throw new ZLinkConfigurationException('MeshNode Actor join dispatch requires the Spot manager.');
      }
      return this.spotManager.dispatchMeshActorJoin(meshName, owner, record);
    }
    if (record.kind === ReceiveKind.ActorBinding) {
      if (record.kindData?.kind !== 'actorBinding') {
        throw new ZLinkConfigurationException(
          'MeshNode Actor binding record has no binding generation.'
        );
      }
      this.actorManager
        ?.getState(record.kindData.actor.actorId)
        ?.setBoundSessionBindingGeneration(record.kindData.bindingGeneration);
      const state = this.actorManager?.getState(record.kindData.actor.actorId);
      const target = this.boundSessionRelay.boundSessions.resolveRemoteBoundSessionTarget(
        record.kindData.sessionNodeRid,
        record.kindData.sessionRid
      );
      if (state !== undefined && target !== undefined) {
        state.setBoundSessionTransferTarget({
          ...target,
          sessionNodeRid: record.kindData.sessionNodeRid,
          sessionRid: record.kindData.sessionRid,
          bindingGeneration: record.kindData.bindingGeneration
        });
      }
      return Promise.resolve();
    }
    switch (record.kind) {
      case ReceiveKind.TransferControl:
        if (record.kindData?.kind !== 'transferControl') {
          throw new ZLinkConfigurationException('MeshNode transfer control record has no typed control payload.');
        }
        return this.actorTransferAuthorityRuntime.handle(meshName, record.kindData, signal);
      case ReceiveKind.ChannelSend:
      case ReceiveKind.ChannelRequest: {
        const channelRuntime = this.channelRuntime;
        if (channelRuntime === undefined) {
          throw new ZLinkConfigurationException('MeshNode channel dispatch requires the channel runtime.');
        }
        return this.admission.run(meshName, 'RouteMesh channel dispatch', () =>
          channelRuntime.dispatchMeshChannel(meshName, record, signal));
      }
      case ReceiveKind.NodeSend:
      case ReceiveKind.NodeRequest: {
        if (record.kind === ReceiveKind.NodeRequest
          && await this.serviceRelocation.tryHandleControl(meshName, record, signal)) {
          return;
        }
        if (record.kind === ReceiveKind.NodeRequest && record.parts.length === 1) {
          const terminal = decodeRemoteActorSourceLeaveTerminal(record.parts[0]!.data());
          if (terminal !== undefined) {
            if (
              this.spotManager?.completeFormalSourceLeaveTerminal(
                terminal.actorId,
                terminal.transferId,
                terminal.succeeded
              ) !== true
            ) {
              throw new ZLinkConfigurationException(
                `Actor '${terminal.actorId}' has no matching formal transfer terminal gate.`
              );
            }
            if (record.reply(Buffer.alloc(0)) !== SubmitResult.Ok) {
              throw new ZLinkConfigurationException(
                `Actor '${terminal.actorId}' formal transfer terminal acknowledgement failed.`
              );
            }
            return Promise.resolve();
          }
        }
        const channelRuntime = this.channelRuntime;
        if (channelRuntime === undefined) {
          throw new ZLinkConfigurationException('MeshNode node-direct dispatch requires the channel runtime.');
        }
        return this.admission.run(meshName, 'RouteMesh node dispatch', () =>
          channelRuntime.dispatchMeshRoute(meshName, record));
      }
      case ReceiveKind.SpotSend:
      case ReceiveKind.SpotRequest:
      case ReceiveKind.SpotMulticast:
        return this.admission.run(meshName, 'RouteMesh Spot dispatch', () =>
          this.dispatchMeshSpotRecord(meshName, owner, record));
      case ReceiveKind.InstanceSpotActivation:
        if (this.spotManager === undefined) {
          throw new ZLinkConfigurationException(
            'MeshNode Instance Spot dispatch requires the Spot manager.'
          );
        }
        return this.admission.run(meshName, 'RouteMesh Instance Spot dispatch', () =>
          this.spotManager!.dispatchMeshInstance(meshName, owner, record));
      case ReceiveKind.SpotControl:
        if (this.spotManager === undefined) {
          throw new ZLinkConfigurationException('MeshNode Spot control dispatch requires the Spot manager.');
        }
        return this.spotManager.dispatchMeshSpotControl(meshName, owner, record);
      case ReceiveKind.ActorSend:
      case ReceiveKind.ActorRequest:
        if (this.spotManager === undefined) {
          throw new ZLinkConfigurationException('MeshNode Actor dispatch requires the Spot manager.');
        }
        return this.admission.run(meshName, 'RouteMesh Actor dispatch', () =>
          this.spotManager!.dispatchMeshActor(meshName, owner, record));
      case ReceiveKind.SendReady:
        this.meshSubmitters.notify(meshName);
        return Promise.resolve();
      default:
        throw new ZLinkConfigurationException(
          `MeshNode record kind '${record.kind}' does not yet have a registered framework consumer.`
        );
    }
  }

  private submitLocalMeshRoute(
    meshName: string,
    sourceNodeRid: string,
    parts: readonly MessageLike[]
  ) {
    const state = this.executionState;
    const channelRuntime = this.channelRuntime;
    if (state === undefined || channelRuntime === undefined || !this.admission.accepts(meshName)) {
      return { status: ZLinkSubmitStatus.Shutdown } as const;
    }
    if (!channelRuntime.canDispatchLocalMeshRoute(meshName)) {
      return { status: ZLinkSubmitStatus.TargetNotFound } as const;
    }
    const inFlight = this.localMeshRouteInFlight.get(meshName) ?? 0;
    if (inFlight >= this.localMeshRouteCapacity) {
      return { status: ZLinkSubmitStatus.Backpressured } as const;
    }
    const claim = this.admission.claim(meshName, 'RouteMesh local node dispatch');
    this.localMeshRouteInFlight.set(meshName, inFlight + 1);
    const ownedParts: BindingMessage[] = [];
    try {
      for (const part of parts) {
        ownedParts.push(BindingMessage.from(
          typeof (part as { data?: unknown }).data === 'function'
            ? (part as { data(): Buffer }).data()
            : part
        ));
      }
    } catch (error) {
      for (const part of ownedParts) part.close();
      claim.close();
      this.releaseLocalMeshRouteSlot(meshName);
      throw error;
    }
    try {
      state.taskRunner.runDetached('mesh-node-local-route-dispatch', async () => {
        try {
          await channelRuntime.dispatchLocalMeshRoute(meshName, sourceNodeRid, ownedParts);
        } finally {
          for (const part of ownedParts) part.close();
          claim.close();
          this.releaseLocalMeshRouteSlot(meshName);
        }
      });
    } catch (error) {
      for (const part of ownedParts) part.close();
      claim.close();
      this.releaseLocalMeshRouteSlot(meshName);
      throw error;
    }
    return { status: ZLinkSubmitStatus.Submitted } as const;
  }

  private releaseLocalMeshRouteSlot(meshName: string): void {
    const remaining = (this.localMeshRouteInFlight.get(meshName) ?? 1) - 1;
    if (remaining === 0) this.localMeshRouteInFlight.delete(meshName);
    else this.localMeshRouteInFlight.set(meshName, remaining);
    this.meshSubmitters.notify(meshName);
  }

  private async dispatchMeshSpotRecord(
    meshName: string,
    owner: ReadyRecord,
    record: ReceiveRecord
  ): Promise<void> {
    if (await this.spotNodeRuntime?.dispatchEntrySpotRecord(meshName, owner, record) === true) {
      return;
    }
    if (this.spotManager === undefined) {
      throw new ZLinkConfigurationException('MeshNode Spot dispatch requires the Spot manager.');
    }
    await this.spotManager.dispatchMeshSpot(meshName, owner, record);
  }

  private createDispatchErrorReporter(errorSink: ZLinkDispatchErrorSink): ZLinkDispatchErrorReporter {
    const existing = this.dispatchErrorReporters.get(errorSink);
    if (existing !== undefined) {
      return existing;
    }
    const reporter = new ZLinkDispatchErrorReporter(
      undefined,
      undefined,
      errorSink,
      this.diagnosticsContext(),
      this.metrics
    );
    this.dispatchErrorReporters.set(errorSink, reporter);
    return reporter;
  }

  private detachedTaskRunner(): ZLinkDetachedTaskRunner {
    return {
      runDetached: (taskName, callback) => {
        const runner = this.executionState?.taskRunner;
        if (runner !== undefined) {
          runner.runDetached(taskName, () => callback());
          return;
        }
        void callback().catch((error) =>
          this.preStartErrorSink.reportRuntimeTaskException(taskName, error));
      }
    };
  }

  private diagnosticsContext(): ZLinkDiagnosticsContext {
    this.cachedDiagnosticsContext ??= createDiagnosticsContext(
      this.options.registration.dispatch,
      this.options.providerResolver,
      this.messageFlowModeCell
    );
    return this.cachedDiagnosticsContext;
  }

  private flowCreationEnabled(): boolean {
    const mode = this.executionState === undefined
      ? this.options.registration.dispatch?.diagnostics.messageFlow ?? ZLinkMessageFlowLogMode.ErrorsOnly
      : this.messageFlowModeCell.mode;
    return mode !== ZLinkMessageFlowLogMode.Off;
  }

  private runLifecycle<T>(operation: () => T): T {
    const current = currentOrCreateFlow('Lifecycle', false);
    const flow = current?.flowOrigin === 'Lifecycle'
      ? current
      : createInboundFlow(undefined, 'Lifecycle', this.flowCreationEnabled());
    return runWithFlow(flow, operation);
  }

  requirePrimaryMeshNode() {
    const node = this.spotNodeRuntime?.primaryMeshNode;
    if (node === undefined) {
      throw new Error('Primary Entry Spot node is not started.');
    }
    return node;
  }

  private ensureLocationRuntime(): ZLinkLocationRuntime | undefined {
    return this.locationOwner.ensureRuntime(this.meshRouters.primaryMeshName());
  }

  private createLocationSpotRouteResolver(): ZLinkSpotRouteResolver | undefined {
    if (this.cachedLocationSpotRouteResolver !== undefined) {
      return this.cachedLocationSpotRouteResolver;
    }
    this.ensureLocationRuntime();
    const legacy = this.locationOwner.createSpotRouteResolver(
      this.meshRouters.spotLocationMeshNames(),
      this.meshRouters.spotRouterChannelIdByMesh(),
      (spotId) => this.spotManager?.resolveLocalSpotRoute(spotId)
    );
    const authority = this.locationOwner.currentStores?.locationStore;
    const resolver = authority === undefined
      ? legacy
      : new ZLinkAuthoritySpotRouteResolver(
          authority,
          this.meshRouters.spotRouterChannelIdByMesh(),
          legacy,
          this.locationOwner.currentLeaseTracker,
          this.options.registration.locations.options.routeCacheMaxAgeMs
        );
    this.cachedLocationSpotRouteResolver = resolver;
    return resolver;
  }

  private createActorLocationResolver(): ZLinkStoreLocationResolvers | undefined {
    return this.locationOwner.createActorLocationResolver(
      this.meshRouters.spotLocationMeshNames()
    );
  }

}

export function hasUnsupportedManualTopology(
  registration: ZLinkFrameworkRegistration
): boolean {
  if ([...registration.routeChannelOptions.values()].some((route) =>
    (route.manualConnections?.length ?? 0) > 0)) {
    return true;
  }

  for (const node of registration.spotNodes.values()) {
    if ((node.router?.manualConnections?.length ?? 0) > 0
      || (node.router?.manualPeerConnections?.length ?? 0) > 0) {
      return true;
    }
  }

  for (const channel of registration.channels.values()) {
    if ((channel.client?.manualConnections?.length ?? 0) > 0
      || (channel.subscriber?.manualConnections?.length ?? 0) > 0) {
      return true;
    }
    if (channel.publisher !== undefined
      && !registration.locations.useInMemoryStores
      && registration.locations.storeInstance === undefined) {
      return true;
    }
  }

  return false;
}

export function hasExactPeerReadiness(
  descriptors: readonly ZLinkMeshNodeDescriptor[],
  local: {
    readonly routingId: unknown;
    readonly lifecycleGeneration: bigint;
    readonly applicationVersion?: bigint;
    readonly maintenanceWave?: string;
  },
  peers: readonly {
    readonly routingId: unknown | null;
    readonly lifecycleGeneration: bigint;
    readonly state: number;
  }[]
): boolean {
  const replacementDescriptors = descriptors.filter((descriptor) =>
    !(String(descriptor.rid) === String(local.routingId)
      && descriptor.lifecycleGeneration === local.lifecycleGeneration)
    && descriptor.state === ZLinkFrameworkRuntimeState.Serving
    && (local.applicationVersion === undefined
      || descriptor.applicationVersion === local.applicationVersion)
    && (local.maintenanceWave === undefined
      || descriptor.maintenanceWave !== local.maintenanceWave));
  return replacementDescriptors.length > 0
    && replacementDescriptors.every((descriptor) =>
      peers.some((peer) => peer.routingId !== null
      && String(peer.routingId) === String(descriptor.rid)
      && peer.lifecycleGeneration === descriptor.lifecycleGeneration
      && peer.state === 3));
}

export {
  ZLinkActorTransferAuthorityRuntime,
  transferIdString
} from './actor-transfer-authority-runtime';
export { ZLinkRouteMeshRuntimeCoordinator } from './route-mesh-runtime';
export {
  ZLinkHostSpotAddressTransport,
  type ZLinkHostSpotAddressTransportOptions
} from './spot-address-transport';
export {
  ZLinkUserSpotCreationCoordinator,
  type ZLinkUserSpotCreationCoordinatorOptions,
  type ZLinkUserSpotCreationRequest,
  type ZLinkUserSpotCreationResult
} from './user-spot-creation-coordinator';

async function waitForForcedSessionNotification(operation: Promise<void> | undefined): Promise<void> {
  if (operation === undefined) return;
  let timeoutHandle: ReturnType<typeof setTimeout> | undefined;
  const timeout = new Promise<void>((resolve) => {
    timeoutHandle = setTimeout(resolve, 100);
  });
  try {
    await Promise.race([operation.catch(() => undefined), timeout]);
  } finally {
    if (timeoutHandle !== undefined) clearTimeout(timeoutHandle);
  }
}

class ZLinkDrainingStatePublishError extends Error {
  constructor(cause: unknown) {
    super('Failed to publish draining peer rows.', { cause });
    this.name = 'ZLinkDrainingStatePublishError';
  }
}

function waitForDrainRetry(delayMs: number, signal: AbortSignal): Promise<void> {
  if (signal.aborted) return Promise.reject(new Error('Drain deadline exceeded.'));
  return new Promise<void>((resolve, reject) => {
    const timeout = setTimeout(() => {
      signal.removeEventListener('abort', onAbort);
      resolve();
    }, delayMs);
    const onAbort = () => {
      clearTimeout(timeout);
      reject(new Error('Drain deadline exceeded.'));
    };
    signal.addEventListener('abort', onAbort, { once: true });
  });
}

function resolveBackendAdapterFactory(internalOptions: unknown): ZLinkBackendAdapterFactory {
  if (
    typeof internalOptions === 'object'
    && internalOptions !== null
    && 'backendAdapterFactory' in internalOptions
  ) {
    const factory = (internalOptions as { readonly backendAdapterFactory?: ZLinkBackendAdapterFactory }).backendAdapterFactory;
    if (factory !== undefined) {
      return factory;
    }
  }
  return new ZLinkNodeBackendAdapterFactory();
}

function resolveStreamPayloadCodec(registration: ZLinkFrameworkRegistration): ZLinkStreamPayloadCodec | undefined {
  const codec = registration.codecs.streamCodecs.values().next().value;
  if (isStreamPayloadCodec(codec)) {
    return codec;
  }
  return undefined;
}

function isStreamPayloadCodec(codec: unknown): codec is ZLinkStreamPayloadCodec {
  return typeof codec === 'object'
    && codec !== null
    && typeof (codec as { encode?: unknown }).encode === 'function';
}

function selectLocationPlacementDescriptor(
  descriptors: readonly ZLinkMeshNodeDescriptor[]
): ZLinkMeshNodeDescriptor | undefined {
  const total = descriptors.reduce(
    (sum, descriptor) => sum + BigInt(descriptor.placementWeight),
    0n
  );
  if (total === 0n) return undefined;
  let point = randomBigIntBelow(total);
  for (const descriptor of descriptors) {
    const weight = BigInt(descriptor.placementWeight);
    if (point < weight) return descriptor;
    point -= weight;
  }
  return descriptors.at(-1);
}

function randomBigIntBelow(exclusiveUpperBound: bigint): bigint {
  if (exclusiveUpperBound <= 0n) {
    throw new RangeError('Random selection upper bound must be positive.');
  }
  const bitLength = exclusiveUpperBound.toString(2).length;
  const byteLength = Math.ceil(bitLength / 8);
  const highBitMask = 0xff >> (byteLength * 8 - bitLength);
  for (;;) {
    const bytes = randomBytes(byteLength);
    bytes[0] = bytes[0]! & highBitMask;
    const value = BigInt(`0x${bytes.toString('hex')}`);
    if (value < exclusiveUpperBound) return value;
  }
}

function relocationReason(reason: string): ZLinkFrameworkRelocationReason {
  switch (reason) {
    case 'deadline_exceeded': return ZLinkFrameworkRelocationReason.DeadlineExceeded;
    case 'store_unavailable': return ZLinkFrameworkRelocationReason.StoreUnavailable;
    case 'target_unavailable': return ZLinkFrameworkRelocationReason.TargetUnavailable;
    default: return ZLinkFrameworkRelocationReason.RelocationFailed;
  }
}

function validateRelocationOptions(
  options: ZLinkFrameworkRelocationOptions | undefined,
  sourceApplicationVersion: bigint
): bigint {
  if (options === undefined) {
    throw new TypeError('Relocation mode is required.');
  }
  const mode = options.mode as number;
  if (mode !== ZLinkFrameworkRelocationMode.PlannedMaintenance
    && mode !== ZLinkFrameworkRelocationMode.RollingUpdate) {
    throw new TypeError('Relocation mode is required.');
  }
  if (options.mode === ZLinkFrameworkRelocationMode.PlannedMaintenance) {
    if (options.targetApplicationVersion !== undefined) {
      throw new TypeError(
        'PlannedMaintenance relocation cannot define targetApplicationVersion.');
    }
    return sourceApplicationVersion;
  }
  if (typeof options.targetApplicationVersion !== 'bigint'
    || options.targetApplicationVersion <= sourceApplicationVersion) {
    throw new TypeError(
      'RollingUpdate relocation requires a targetApplicationVersion greater than the source version.');
  }
  return options.targetApplicationVersion;
}

function blockedRelocation(
  mode: ZLinkFrameworkRelocationMode,
  effectiveTargetApplicationVersion: bigint,
  reason: ZLinkFrameworkRelocationReason
): ZLinkFrameworkRelocationResult {
  return {
    mode,
    effectiveTargetApplicationVersion,
    outcome: ZLinkFrameworkRelocationOutcome.Blocked,
    reason
  };
}

function waitForRuntimeOperation<T>(
  operation: Promise<T>,
  signal?: AbortSignal
): Promise<T> {
  if (signal === undefined) return operation;
  signal.throwIfAborted();
  return new Promise((resolve, reject) => {
    const aborted = () => reject(signal.reason);
    signal.addEventListener('abort', aborted, { once: true });
    operation.then(resolve, reject).finally(() => signal.removeEventListener('abort', aborted));
  });
}
