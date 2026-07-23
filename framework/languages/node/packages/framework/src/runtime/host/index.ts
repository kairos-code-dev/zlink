import { randomUUID } from 'node:crypto';
import {
  Message as BindingMessage,
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
import type { ZLinkFrameworkRegistration } from '../configuration';
import type {
  ActorRef,
  ZLinkProviderResolver,
  ZLinkRouteMeshRuntime,
  ZLinkRuntimeEventPublisher
} from '../../contracts';
import type { ZLinkSpotRouteResolver } from '../spots/spot-routing-internal';
import {
  ZLinkMessageFlowLogMode,
  ZLinkSubmitStatus
} from '../../contracts';
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
  DefaultZLinkSpotManager,
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
  forwardEncodedActorPacket,
  ZLinkActorHandoffCoordinator,
  ZLinkActorTransferRegistry,
  decodeRemoteActorSourceLeaveTerminal
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
  ZLinkAllocatedRoutingIdRuntime,
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
import { collectRoutingIdAllocationMembers } from '../../contracts/Configuration/RoutingIdAllocationRegistration';
import { ZLinkConfigurationException } from '../../contracts/Configuration/ConfigurationException';
import type { ZLinkAllocatedRoutingId, ZLinkAllocatedRoutingIdProvider } from '../../contracts/Locations';
import { ZLinkMeshSubmitterRegistry } from '../messaging';

export interface ZLinkFrameworkRuntime {
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
  ZLinkFrameworkRuntime,
  ZLinkMessageFlowControl,
  ZLinkAllocatedRoutingIdProvider {
  private readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  private readonly lifecycleSink?: string[];
  private state?: ZLinkFrameworkExecutionState;
  private channelRuntime?: ZLinkChannelRuntimeManager;
  private spotNodeRuntime?: ZLinkSpotNodeRuntimeManager;
  private streamRuntime?: ZLinkStreamRuntimeManager;
  private monitoringRuntime?: ZLinkMonitoringRuntime;
  private allocatedRoutingIdRuntime?: ZLinkAllocatedRoutingIdRuntime;
  private readonly locationOwner: ZLinkLocationRuntimeOwner;
  private readonly meshRouters: MeshRouterResolver;
  private readonly boundSessionRelay: ZLinkBoundSessionRelay;
  private readonly actorHandoff: ZLinkActorHandoffCoordinator;
  private readonly actorTransferRegistry: ZLinkActorTransferRegistry;
  private readonly actorTransferRuntime: ZLinkActorTransferRuntime;
  private readonly actorTransferAuthorityRuntime: ZLinkActorTransferAuthorityRuntime;
  private readonly entryActorRuntime: ZLinkEntryActorRuntimeService;
  private actorManager?: DefaultZLinkActorManager;
  private spotManager?: DefaultZLinkSpotManager;
  private readonly destroyedActorRefs = new Map<string, ActorRef>();
  private readonly runtimeEventPublisher: ZLinkRuntimeEventPublisher;
  private readonly metrics: ZLinkRuntimeMetrics;
  private readonly admission = new ZLinkRuntimeAdmissionGate();
  private readonly meshSubmitters = new ZLinkMeshSubmitterRegistry();
  private readonly localMeshRouteInFlight = new Map<string, number>();
  private readonly localMeshRouteCapacity = 4096;
  private readonly allocatedRoutingIdGroupNames: ReadonlySet<string>;
  private allocationRuntimeReady!: Promise<ZLinkAllocatedRoutingIdRuntime>;
  private resolveAllocationRuntime!: (runtime: ZLinkAllocatedRoutingIdRuntime) => void;
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
  readonly routeTransport: ZLinkRuntimeRouteTransport;
  readonly spotPublisherTransport = new ZLinkRuntimeSpotPublisherTransport(() => this.spotNodeRuntime);
  readonly streamBindingRuntime: ZLinkStreamBindingRuntime;
  readonly boundSessionFactory: DefaultZLinkBoundSessionFactory;
  readonly spotRouterChannelIdForMesh: (meshName: string) => string;
  readonly routeMeshRuntime: ZLinkRouteMeshRuntime;
  private readonly routeMeshCoordinator: ZLinkRouteMeshRuntimeCoordinator;

  constructor(readonly options: ZLinkFrameworkRuntimeHostOptions, internalOptions?: unknown) {
    this.backendAdapterFactory = resolveBackendAdapterFactory(internalOptions);
    this.lifecycleSink = options.lifecycleSink;
    this.runtimeEventPublisher = options.runtimeEventPublisher ?? new DefaultZLinkRuntimeEventPublisher();
    this.metrics = new ZLinkRuntimeMetrics(options.registration.metrics?.meterProvider);
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
    this.spotRouterChannelIdForMesh = this.meshRouters.spotRouterChannelIdByMesh();
    this.allocatedRoutingIdGroupNames = new Set(
      collectRoutingIdAllocationMembers(options.registration).map((member) => member.groupName)
    );
    this.locationOwner = new ZLinkLocationRuntimeOwner({
      registration: options.registration,
      runtimeEventPublisher: this.runtimeEventPublisher,
      metrics: this.metrics,
      fallbackNodeRid: `node-${randomUUID()}`
    });
    this.resetAllocationRuntimeReady();
    this.streamBindingRuntime = new ZLinkStreamBindingRuntime({
      streamPayloadCodec: resolveStreamPayloadCodec(options.registration),
      streamCompression: options.registration.streamCompression,
      messageSerializers: options.registration.messageSerializers,
      metrics: this.metrics,
      flowCreationEnabled: () => this.flowCreationEnabled(),
      nativeActorNodeProvider: () => this.spotNodeRuntime?.primaryMeshNode,
      meshSubmitters: this.meshSubmitters,
      nativeActorMeshNameProvider: () => this.meshRouters.primaryMeshName()
    });
    this.actorHandoff = new ZLinkActorHandoffCoordinator({
      routedTransport: this.routeTransport,
      forwardActorPacket: async (_actorId, actor, packet) => {
        const completions = this.spotNodeRuntime?.primaryMeshCompletions;
        if (completions === undefined) {
          throw new Error('Actor handoff requires a running MeshNode completion table.');
        }
        return await forwardEncodedActorPacket(
          this.requirePrimaryMeshNode(),
          completions,
          actor,
          Buffer.from(packet.header, 'base64'),
          Buffer.from(packet.payload, 'base64'),
          packet.returnResponse,
          options.registration.requestTimeoutMs,
          options.registration.messageSerializers
        );
      },
      forwardWindowMs: options.registration.actorTransferForwardWindowMs,
      requestTimeoutMs: options.registration.requestTimeoutMs,
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
          current.generation !== actorRef.generation ||
          !routingIdsEqual(current.nodeRid, actorRef.nodeRid)
        );
      },
      isCurrentHandoffTarget: (actorId, spotRid) => {
        const currentSpotRid = this.actorManager?.getState(actorId)?.spotRid;
        return routingIdsEqual(currentSpotRid, spotRid);
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
      shutdownSignal: () => this.state?.abortController.signal
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
      clearRemoteActorPacketTarget: (actorId) =>
        this.boundSessionRelay.clearRemoteActorPacketTarget(actorId),
      reportPostCommitError: (error) =>
        (this.errorSink ?? this.preStartErrorSink).reportRuntimeTaskException('source actor departure', error),
      onSourceDepartureCompleted: (actorId) =>
        this.publishActorHandoffEvent({ marker: 'source_cleanup', actorId }),
      shutdownSignal: () => this.state?.abortController.signal,
      metrics: this.metrics
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
      admission: this.admission,
      publishDraining: (meshName, signal) => this.publishDraining(meshName, signal),
      drainResources: (meshName, signal) => this.performMeshDrain(meshName, signal),
      forceStopResources: (meshName) => this.forceStopMesh(meshName)
    });
    this.routeMeshRuntime = this.routeMeshCoordinator;
  }

  get isStarted(): boolean {
    return this.state !== undefined;
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

  async waitForReadyAllocation(groupName: string, signal?: AbortSignal): Promise<ZLinkAllocatedRoutingId> {
    if (!this.allocatedRoutingIdGroupNames.has(groupName)) {
      throw new ZLinkConfigurationException(
        `Routing-id allocation group '${groupName}' is not registered.`
      );
    }
    const runtime = await waitForAllocationRuntime(this.allocationRuntimeReady, signal);
    return await runtime.waitForReadyAllocation(groupName, signal);
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
    return this.state?.context as ZLinkBackendContext | undefined;
  }

  get taskRunner(): ZLinkFrameworkExecutionState['taskRunner'] | undefined {
    return this.state?.taskRunner;
  }

  get errorSink(): ZLinkFrameworkExecutionState['errorSink'] | undefined {
    return this.state?.errorSink;
  }

  async start(): Promise<void> {
    await this.runLifecycle(() => this.startCore());
  }

  private async startCore(): Promise<void> {
    if (this.state !== undefined) {
      return;
    }

    this.lifecycleSink?.push('framework:start');
    const channelAdapter = this.backendAdapterFactory.createChannelAdapter();
    const context = channelAdapter.createContext();
    let channelRuntime: ZLinkChannelRuntimeManager | undefined;
    let spotNodeRuntime: ZLinkSpotNodeRuntimeManager | undefined;
    let streamRuntime: ZLinkStreamRuntimeManager | undefined;
    let startedLocationRuntime: ZLinkLocationRuntime | undefined;
    let allocatedRoutingIdRuntime: ZLinkAllocatedRoutingIdRuntime | undefined;
    try {
      this.state = new ZLinkFrameworkExecutionState(context);
      // Seed the shared live-mode cell from the configured mode (default errorsOnly).
      this.messageFlowModeCell.mode =
        this.options.registration.dispatch?.diagnostics.messageFlow ??
        ZLinkMessageFlowLogMode.ErrorsOnly;
      const dispatchErrors = this.createDispatchErrorReporter(this.state.errorSink);
      if (this.allocatedRoutingIdGroupNames.size > 0) {
        const locationRuntime = this.locationOwner.ensureRuntime(this.meshRouters.primaryMeshName());
        const allocationStore = this.locationOwner.routingIdAllocationStore();
        if (locationRuntime === undefined || allocationStore === undefined) {
          throw new ZLinkConfigurationException(
            'Allocated routing ids require a location store with slot-allocation capability.'
          );
        }
        allocatedRoutingIdRuntime = new ZLinkAllocatedRoutingIdRuntime(
          this.options.registration,
          allocationStore,
          locationRuntime,
          (error) => {
            queueMicrotask(() => {
              void this.stop().catch((stopError) =>
                this.runtimeOrPreStartErrorSink.reportRuntimeTaskException(
                  'allocated routing-id fencing',
                  new AggregateError([error, stopError])
                ));
            });
          }
        );
        this.allocatedRoutingIdRuntime = allocatedRoutingIdRuntime;
        this.resolveAllocationRuntime(allocatedRoutingIdRuntime);
        await allocatedRoutingIdRuntime.start(this.state.abortController.signal);
        await locationRuntime.start(this.locationOwner.ownerNodeRid(), this.state.abortController.signal);
        startedLocationRuntime = locationRuntime;
      }
      channelRuntime = new ZLinkChannelRuntimeManager(
        this.options.registration,
        channelAdapter,
        context,
        this.options.providerResolver,
        this.createChannelRuntimeOptions()
      );
      this.channelRuntime = channelRuntime;
      channelRuntime.prepareMeshDispatch(this.state.taskRunner);
      spotNodeRuntime = new ZLinkSpotNodeRuntimeManager(
        this.createSpotNodeRuntimeOptions(context, dispatchErrors)
      );
      await spotNodeRuntime.start();
      channelRuntime.bindRouteMeshRouters();
      // Start bound receivers before publishing Serving descriptors. A
      // discovered ClientServer endpoint must already be able to dispatch.
      this.state.listenerTasks.push(...channelRuntime.start(this.state.taskRunner));
      const locationRuntime = await this.locationOwner.startForRuntime(
        this.meshRouters.primaryMeshName(),
        spotNodeRuntime,
        channelRuntime
      );
      startedLocationRuntime = locationRuntime;
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
          meshNodes: spotNodeRuntime.meshNodesByName,
          locationRuntime,
          monitoringAdapter: this.backendAdapterFactory.createMonitoringAdapter(),
          publisher: this.runtimeEventPublisher
        });
        this.monitoringRuntime.start(this.state);
      }
      this.routeMeshCoordinator.markServing();
      this.lifecycleSink?.push('framework:started');
      allocatedRoutingIdRuntime?.markReady();
    } catch (error) {
      allocatedRoutingIdRuntime?.fail(error);
      await rollbackRuntimeStart({
        context,
        startedLocationRuntime,
        monitoringRuntime: this.monitoringRuntime,
        streamRuntime,
        spotNodeRuntime,
        channelRuntime,
        allocatedRoutingIdRuntime
      });
      this.state = undefined;
      this.channelRuntime = undefined;
      this.spotNodeRuntime = undefined;
      this.streamRuntime = undefined;
      this.monitoringRuntime = undefined;
      this.allocatedRoutingIdRuntime = undefined;
      this.locationOwner.clearForStop();
      this.resetAllocationRuntimeReady();
      throw error;
    }
  }

  async stop(): Promise<void> {
    await this.runLifecycle(() => this.stopCore());
  }

  private async stopCore(): Promise<void> {
    const state = this.state;
    if (state === undefined) {
      return;
    }

    const channelRuntime = this.channelRuntime;
    const spotNodeRuntime = this.spotNodeRuntime;
    const streamRuntime = this.streamRuntime;
    const monitoringRuntime = this.monitoringRuntime;
    const allocatedRoutingIdRuntime = this.allocatedRoutingIdRuntime;
    const locationSnapshot = this.locationOwner.clearForStop();
    this.state = undefined;
    this.channelRuntime = undefined;
    this.spotNodeRuntime = undefined;
    this.streamRuntime = undefined;
    this.monitoringRuntime = undefined;
    this.allocatedRoutingIdRuntime = undefined;
    this.lifecycleSink?.push('framework:stop');
    await stopRuntimeParts({
      state,
      locationSnapshot,
      monitoringRuntime,
      streamRuntime,
      spotNodeRuntime,
      channelRuntime,
      allocatedRoutingIdRuntime
    });
    this.meshSubmitters.dispose();
    this.resetAllocationRuntimeReady();
    this.lifecycleSink?.push('framework:stopped');
  }

  async onApplicationBootstrap(): Promise<void> {
    await this.start();
  }

  async onApplicationShutdown(): Promise<void> {
    const meshNames = [...this.options.registration.spotNodes.keys()];
    if (meshNames.length === 1) {
      await this.routeMeshRuntime.drain(meshNames[0]!);
    }
    await this.stop();
  }

  private async performMeshDrain(meshName: string, signal: AbortSignal): Promise<void> {
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
    await this.cleanupOwnerForDrain(signal);
  }

  private async publishDraining(meshName: string, signal: AbortSignal): Promise<void> {
    const node = this.spotNodeRuntime?.meshNode(meshName);
    for (const channelName of Object.keys(
      this.options.registration.spotNodes.get(meshName)?.meshChannels ?? {}
    )) {
      node?.setChannelWeight(channelName, 0);
    }
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
      if (handedOffActorIds.has(actor.actorId)) continue;
      if (state.isMoving) {
        allMoved = false;
        continue;
      }
      const meshName = this.meshRouters.actorMeshName(actorType);
      if (meshName !== drainingMeshName) continue;
      try {
        const target = await this.locationOwner.createRefResolver([meshName])
          ?.selectActorPlacement(meshName, actorType, sourceNodeRid, signal);
        if (target === undefined) {
          allMoved = false;
          continue;
        }
        const result = await actor.context.joinEntrySpot(target, undefined).submit(signal);
        if (result.status !== 'accepted') {
          allMoved = false;
          continue;
        }
        handedOffActorIds.add(actor.actorId);
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
      spotPublisherTransport: this.spotPublisherTransport,
      meshRouters: this.meshRouters,
      runtimeEventPublisher: this.runtimeEventPublisher,
      spotNodeRuntime: () => this.spotNodeRuntime,
      actorManager: () => this.actorManager,
      locationLifecycle: () => this.locationOwner.currentLifecycle,
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
        this.spotNodeRuntime?.notifyEntrySpotActorCreated(nodeRid, actor, createRequest, signal) ?? Promise.resolve(),
      locationLifecycle: () => this.locationOwner.currentLifecycle,
      primaryMeshName: () => this.meshRouters.primaryMeshName(),
      actorMeshName: (actorType) => this.meshRouters.actorMeshName(actorType),
      createLocationSpotRouteResolver: () => this.createLocationSpotRouteResolver(),
      createActorLocationResolver: () => this.createActorLocationResolver(),
      forgetDestroyedActorRef: (actorId) => this.destroyedActorRefs.delete(actorId),
      rememberDestroyedActorRef: (actorId, actorRef) => this.destroyedActorRefs.set(actorId, actorRef),
      reportPostCommitError: (error) =>
        this.runtimeOrPreStartErrorSink.reportRuntimeTaskException('post-commit actor binding', error),
      reportBoundSessionSendError: (error) =>
        this.runtimeOrPreStartErrorSink.reportRuntimeTaskException('bound session one-way submit', error),
      actorHandoff: this.actorHandoff,
      actorTransferRuntime: this.actorTransferRuntime,
      actorTransferRegistry: this.actorTransferRegistry,
      shutdownSignal: () => this.state?.abortController.signal,
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
        this.dispatchMeshRecord(meshName, owner, record, this.state?.abortController.signal)
    };
  }

  private dispatchMeshRecord(
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
    const state = this.state;
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
        const runner = this.state?.taskRunner;
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
    const mode = this.state === undefined
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
    this.ensureLocationRuntime();
    return this.locationOwner.createSpotRouteResolver(
      this.meshRouters.spotLocationMeshNames(),
      this.meshRouters.spotRouterChannelIdByMesh(),
      (spotRid) => this.spotManager?.resolveLocalSpotRoute(spotRid)
    );
  }

  private createActorLocationResolver(): ZLinkStoreLocationResolvers | undefined {
    return this.locationOwner.createActorLocationResolver(
      this.meshRouters.spotLocationMeshNames()
    );
  }

  private resetAllocationRuntimeReady(): void {
    this.allocationRuntimeReady = new Promise((resolve) => {
      this.resolveAllocationRuntime = resolve;
    });
  }

}

export {
  ZLinkActorTransferAuthorityRuntime,
  transferIdString
} from './actor-transfer-authority-runtime';
export { ZLinkRouteMeshRuntimeCoordinator } from './route-mesh-runtime';

function waitForAllocationRuntime<T>(operation: Promise<T>, signal?: AbortSignal): Promise<T> {
  if (signal === undefined) return operation;
  if (signal.aborted) return Promise.reject(signal.reason);
  return new Promise<T>((resolve, reject) => {
    const aborted = () => reject(signal.reason);
    signal.addEventListener('abort', aborted, { once: true });
    operation.then(resolve, reject).finally(() => {
      signal.removeEventListener('abort', aborted);
    }).catch(() => undefined);
  });
}

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
