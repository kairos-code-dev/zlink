import { randomUUID } from 'node:crypto';
import { ZLinkNodeBackendAdapterFactory } from '../backend';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkBackendContext,
  ZLinkBackendSpotNode
} from '../backend';
import type { ZLinkFrameworkRegistration } from '../configuration';
import type {
  ActorRef,
  ZLinkProviderResolver,
  ZLinkRuntimeEventPublisher,
  ZLinkDrainControl,
  ZLinkDrainResult
} from '../../contracts';
import type { ZLinkSpotRouteResolver } from '../spots/spot-routing-internal';
import {
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind,
  ZLinkMessageFlowLogMode,
  ZLinkMessageFlowOutcome
} from '../../contracts';
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
  ZLinkFrameworkRuntimeState,
  ZLinkRuntimeErrorSink
} from '../execution';
import {
  createDiagnosticsContext,
  DefaultZLinkRuntimeEventPublisher,
  flowIfEnabled,
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
  ZLinkActorHandoffCoordinator,
  ZLinkActorTransferRegistry
} from '../actors';
import {
  DefaultZLinkBoundSessionFactory,
  type DefaultZLinkBoundSession,
  type ZLinkStreamPayloadCodec,
  ZLinkStreamBindingRuntime,
  ZLinkStreamRuntimeManager
} from '../streams';
import type {
  ZLinkLocationRuntime,
  ZLinkStoreLocationResolvers
} from '../locations';
import {
  zlinkDefaultLocationOptions,
  type ZLinkLocationRuntimeQuery
} from '../../contracts/Locations';
import { ZLinkMonitoringRuntime } from './monitoring-runtime';
import { ZLinkActorRuntimeOptionsFactory } from './actor-runtime-options-factory';
import { ZLinkActorTransferRuntime } from './actor-transfer-runtime';
import { ZLinkEntryActorRuntimeService } from './entry-actor-runtime';
import { ZLinkLocationRuntimeOwner } from './location-runtime-owner';
import { MeshRouterResolver } from './mesh-router-resolver';
import { ZLinkBoundSessionRelay } from './bound-session-relay';
import { routingIdsEqual } from '../routing-id';
import { ZLinkSpotRuntimeOptionsFactory } from './spot-runtime-options-factory';
import { ZLinkChannelRuntimeOptionsFactory } from './channel-runtime-options-factory';
import { ZLinkSpotNodeRuntimeOptionsFactory } from './spot-node-runtime-options-factory';
import { rollbackRuntimeStart, stopRuntimeParts } from './runtime-shutdown';

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

export class ZLinkFrameworkRuntimeHost implements ZLinkFrameworkRuntime, ZLinkMessageFlowControl, ZLinkDrainControl {
  private readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  private readonly lifecycleSink?: string[];
  private state?: ZLinkFrameworkRuntimeState;
  private channelRuntime?: ZLinkChannelRuntimeManager;
  private spotNodeRuntime?: ZLinkSpotNodeRuntimeManager;
  private streamRuntime?: ZLinkStreamRuntimeManager;
  private monitoringRuntime?: ZLinkMonitoringRuntime;
  private readonly locationOwner: ZLinkLocationRuntimeOwner;
  private readonly meshRouters: MeshRouterResolver;
  private readonly boundSessionRelay: ZLinkBoundSessionRelay;
  private readonly actorHandoff: ZLinkActorHandoffCoordinator;
  private readonly actorTransferRegistry: ZLinkActorTransferRegistry;
  private readonly actorTransferRuntime: ZLinkActorTransferRuntime;
  private readonly entryActorRuntime: ZLinkEntryActorRuntimeService;
  private actorManager?: DefaultZLinkActorManager;
  private spotManager?: DefaultZLinkSpotManager;
  private readonly destroyedActorRefs = new Map<string, ActorRef>();
  private readonly runtimeEventPublisher: ZLinkRuntimeEventPublisher;
  private readonly metrics: ZLinkRuntimeMetrics;
  private ready = true;
  private drainOperation?: Promise<ZLinkDrainResult>;
  private readonly drainedWaiters: Array<(result: ZLinkDrainResult) => void> = [];
  private drainMetricState = 'serving';
  private drainStartedAt?: bigint;
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
  private readonly preStartErrorSink = new ZLinkRuntimeErrorSink();
  readonly channelTransport = new ZLinkRuntimeChannelTransport(() => this.channelRuntime);
  readonly channelRuntimeOptions = new DefaultZLinkChannelRuntimeOptions(() => this.channelRuntime);
  readonly routeTransport = new ZLinkRuntimeRouteTransport(
    () => this.channelRuntime,
    (routerChannelId) => this.meshRouters.canUseRouterChannel(routerChannelId)
  );
  readonly spotPublisherTransport = new ZLinkRuntimeSpotPublisherTransport(() => this.spotNodeRuntime);
  readonly streamBindingRuntime: ZLinkStreamBindingRuntime;
  readonly boundSessionFactory: DefaultZLinkBoundSessionFactory;

  constructor(readonly options: ZLinkFrameworkRuntimeHostOptions, internalOptions?: unknown) {
    this.backendAdapterFactory = resolveBackendAdapterFactory(internalOptions);
    this.lifecycleSink = options.lifecycleSink;
    this.runtimeEventPublisher = options.runtimeEventPublisher ?? new DefaultZLinkRuntimeEventPublisher();
    this.metrics = new ZLinkRuntimeMetrics(options.registration.metrics?.meterProvider);
    this.metrics.change('zlink.drain.state', 1, { state: 'serving' });
    this.meshRouters = new MeshRouterResolver(options.registration);
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
      nativeActorNodeProvider: () => this.spotNodeRuntime?.primaryNode,
      relay: (actor, header, payload, signal) =>
        this.boundSessionRelay.actorPackets.relayActorPacket(actor, header, payload, signal),
      notifyDisconnected: (actor, signal) =>
        this.boundSessionRelay.actorPackets.notifyBoundActorDisconnected(actor, signal)
    });
    this.actorHandoff = new ZLinkActorHandoffCoordinator({
      routedTransport: this.routeTransport,
      forwardWindowMs: options.registration.actorTransferForwardWindowMs,
      requestTimeoutMs: options.registration.requestTimeoutMs,
      onMarker: (marker, actorId, index) => {
        void this.runtimeEventPublisher.publish({
          sourceName: 'zlink.framework.actor-handoff',
          timestamp: new Date(),
          marker,
          actorId,
          index
        });
      },
      onRequestFrame: (actorId, index, requestSeq, flags) => {
        void this.runtimeEventPublisher.publish({
          sourceName: 'zlink.framework.actor-handoff',
          timestamp: new Date(),
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
      options.registration.messageSerializers
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
      primarySpotNode: () => this.requirePrimarySpotNode(),
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
      primarySpotNode: () => this.requirePrimarySpotNode(),
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
      shutdownSignal: () => this.state?.abortController.signal,
      metrics: this.metrics
    });
  }

  get isStarted(): boolean {
    return this.state !== undefined;
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
    return this.state?.context as ZLinkBackendContext | undefined;
  }

  get taskRunner(): ZLinkFrameworkRuntimeState['taskRunner'] | undefined {
    return this.state?.taskRunner;
  }

  get errorSink(): ZLinkFrameworkRuntimeState['errorSink'] | undefined {
    return this.state?.errorSink;
  }

  async start(): Promise<void> {
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
    try {
      this.state = new ZLinkFrameworkRuntimeState(context);
      // Seed the shared live-mode cell from the configured mode (default errorsOnly).
      this.messageFlowModeCell.mode =
        this.options.registration.dispatch?.diagnostics.messageFlow ??
        ZLinkMessageFlowLogMode.ErrorsOnly;
      const dispatchErrors = this.createDispatchErrorReporter(this.state.errorSink);
      channelRuntime = new ZLinkChannelRuntimeManager(
        this.options.registration,
        channelAdapter,
        context,
        this.options.providerResolver,
        this.createChannelRuntimeOptions()
      );
      this.channelRuntime = channelRuntime;
      spotNodeRuntime = new ZLinkSpotNodeRuntimeManager(
        this.createSpotNodeRuntimeOptions(context, dispatchErrors)
      );
      await spotNodeRuntime.start();
      channelRuntime.bindRouteMeshRouters();
      const locationRuntime = await this.locationOwner.startForRuntime(
        this.meshRouters.primarySpotMeshName(),
        spotNodeRuntime,
        channelRuntime
      );
      startedLocationRuntime = locationRuntime;
      channelRuntime.setSpotNodes(spotNodeRuntime.nodesByName);
      this.state.listenerTasks.push(...channelRuntime.start(this.state.taskRunner));
      this.spotNodeRuntime = spotNodeRuntime;
      streamRuntime = new ZLinkStreamRuntimeManager({
        registration: this.options.registration,
        backendAdapterFactory: this.backendAdapterFactory,
        context,
        bindingRuntime: this.streamBindingRuntime,
        spotNodes: spotNodeRuntime.nodesByName,
        providerResolver: this.options.providerResolver,
        dispatchErrors,
        metrics: this.metrics
      });
      streamRuntime.start();
      this.streamRuntime = streamRuntime;
      if (this.options.registration.monitoring !== undefined) {
        this.monitoringRuntime = new ZLinkMonitoringRuntime({
          registration: this.options.registration,
          channelRuntime,
          spotNodes: spotNodeRuntime.nodesByName,
          locationRuntime,
          monitoringAdapter: this.backendAdapterFactory.createMonitoringAdapter(),
          publisher: this.runtimeEventPublisher
        });
        this.monitoringRuntime.start(this.state);
      }
      this.lifecycleSink?.push('framework:started');
    } catch (error) {
      await rollbackRuntimeStart({
        context,
        startedLocationRuntime,
        monitoringRuntime: this.monitoringRuntime,
        streamRuntime,
        spotNodeRuntime,
        channelRuntime
      });
      throw error;
    }
  }

  async stop(): Promise<void> {
    const state = this.state;
    if (state === undefined) {
      return;
    }

    const channelRuntime = this.channelRuntime;
    const spotNodeRuntime = this.spotNodeRuntime;
    const streamRuntime = this.streamRuntime;
    const monitoringRuntime = this.monitoringRuntime;
    const locationSnapshot = this.locationOwner.clearForStop();
    this.state = undefined;
    this.channelRuntime = undefined;
    this.spotNodeRuntime = undefined;
    this.streamRuntime = undefined;
    this.monitoringRuntime = undefined;
    this.lifecycleSink?.push('framework:stop');
    await stopRuntimeParts({
      state,
      locationSnapshot,
      monitoringRuntime,
      streamRuntime,
      spotNodeRuntime,
      channelRuntime
    });
    this.lifecycleSink?.push('framework:stopped');
  }

  async onApplicationBootstrap(): Promise<void> {
    await this.start();
  }

  async onApplicationShutdown(): Promise<void> {
    await this.drain();
  }

  isReady(): boolean {
    return this.ready;
  }

  drain(deadlineMs = 30_000, signal?: AbortSignal): Promise<ZLinkDrainResult> {
    if (!Number.isFinite(deadlineMs) || deadlineMs <= 0) {
      return Promise.reject(new TypeError('Drain deadlineMs must be greater than zero.'));
    }
    this.drainOperation ??= this.performDrain(deadlineMs).then((result) => {
      for (const resolve of this.drainedWaiters.splice(0)) resolve(result);
      return result;
    });
    return waitForDrain(this.drainOperation, signal);
  }

  awaitDrained(signal?: AbortSignal): Promise<ZLinkDrainResult> {
    const operation = this.drainOperation ?? new Promise<ZLinkDrainResult>((resolve) => {
      this.drainedWaiters.push(resolve);
    });
    return waitForDrain(operation, signal);
  }

  private async performDrain(deadlineMs: number): Promise<ZLinkDrainResult> {
    this.drainStartedAt = process.hrtime.bigint();
    this.ready = false;
    const deadline = new AbortController();
    let timeoutHandle: ReturnType<typeof setTimeout> | undefined;
    const timeout = new Promise<{ readonly kind: 'timeout' }>((resolve) => {
      timeoutHandle = setTimeout(() => {
        deadline.abort();
        resolve({ kind: 'timeout' });
      }, deadlineMs);
    });
    try {
      const graceful = this.performGracefulDrain(deadline.signal).then(
        () => ({ kind: 'drained' } as const),
        (error: unknown) => ({ kind: 'failed', error } as const)
      );
      const outcome = await Promise.race([graceful, timeout]);
      if (outcome.kind === 'timeout') {
        return await this.forceStopDrain('DeadlineExceeded');
      }
      if (outcome.kind === 'failed') {
        if (deadline.signal.aborted) {
          return await this.forceStopDrain('DeadlineExceeded');
        }
        const reason = outcome.error instanceof ZLinkDrainingStatePublishError
          ? 'DrainingStatePublishFailed'
          : 'TeardownFailed';
        return await this.forceStopDrain(reason);
      }
      const result = { kind: 'drained' } as const;
      await this.publishDrainState('Drained', result);
      return result;
    } finally {
      if (timeoutHandle !== undefined) clearTimeout(timeoutHandle);
    }
  }

  private async performGracefulDrain(signal: AbortSignal): Promise<void> {
    try {
      if (this.locationOwner.currentRuntime !== undefined && !await this.locationOwner.currentRuntime.publishDraining()) {
        throw new Error('Failed to publish draining peer rows.');
      }
    } catch (error) {
      throw new ZLinkDrainingStatePublishError(error);
    }
    await this.publishDrainState('Draining');
    await this.streamRuntime?.notifyServerDrain();
    const handedOffActorIds = new Set<string>();
    while (!await this.handoffActorsForDrain(handedOffActorIds, signal)) {
      await waitForDrainRetry(
        this.options.registration.locations.options.pollingIntervalMs
          ?? zlinkDefaultLocationOptions.pollingIntervalMs,
        signal
      );
    }
    await this.drainSpots(signal);
    await this.stop();
  }

  private async forceStopDrain(reason: import('../../contracts').ZLinkDrainForceReason): Promise<ZLinkDrainResult> {
    let terminalReason = reason;
    try {
      await this.stop();
    } catch {
      terminalReason = 'TeardownFailed';
    }
    const result = { kind: 'force-stopped', reason: terminalReason } as const;
    await this.publishDrainState('ForceStopping', result).catch(() => undefined);
    return result;
  }

  private async drainSpots(signal?: AbortSignal): Promise<void> {
    const manager = this.spotManager;
    if (manager === undefined) return;
    await manager.drainForShutdown(signal);
  }

  private async handoffActorsForDrain(
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
      if (this.spotManager?.retainsActorDuringDrain(state.spotRid) === true) continue;
      const meshName = this.meshRouters.actorSpotMeshName(actorType);
      if (meshName === undefined) {
        allMoved = false;
        continue;
      }
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

  private publishDrainState(state: import('../../contracts').ZLinkDrainState, result?: ZLinkDrainResult): Promise<void> {
    const metricState = drainMetricState(state);
    if (metricState !== this.drainMetricState) {
      this.metrics.change('zlink.drain.state', -1, { state: this.drainMetricState });
      this.metrics.change('zlink.drain.state', 1, { state: metricState });
      this.drainMetricState = metricState;
    }
    if (state === 'Drained' || state === 'ForceStopping') {
      if (this.drainStartedAt !== undefined) {
        this.metrics.duration('zlink.drain.duration', Number(process.hrtime.bigint() - this.drainStartedAt) / 1e9);
      }
      if (state === 'ForceStopping') this.metrics.count('zlink.drain.forced');
    }
    return this.runtimeEventPublisher.publish({
      sourceName: 'drain',
      timestamp: new Date(),
      state,
      result
    });
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
    | 'messageSerializers'
    | 'nativeActorNode'
    | 'nativeActorNodeProvider'
    | 'actorCreatedNodeRidProvider'
    | 'actorCreatedNotifier'
    | 'actorDestroyedCleanup'
    | 'locationLifecycle'
    | 'boundSessionFactory'
    | 'shutdownSignal'
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
      metrics: this.metrics
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
      primarySpotNode: () => this.requirePrimarySpotNode(),
      primarySpotNodeOrUndefined: () => this.spotNodeRuntime?.primaryNode,
      notifyEntrySpotActorCreated: (nodeRid, actor, createRequest, signal) =>
        this.spotNodeRuntime?.notifyEntrySpotActorCreated(nodeRid, actor, createRequest, signal) ?? Promise.resolve(),
      locationLifecycle: () => this.locationOwner.currentLifecycle,
      primarySpotMeshName: () => this.meshRouters.primarySpotMeshName(),
      createLocationSpotRouteResolver: () => this.createLocationSpotRouteResolver(),
      createActorLocationResolver: () => this.createActorLocationResolver(),
      forgetDestroyedActorRef: (actorId) => this.destroyedActorRefs.delete(actorId),
      rememberDestroyedActorRef: (actorId, actorRef) => this.destroyedActorRefs.set(actorId, actorRef),
      reportPostCommitError: (error) =>
        this.runtimeOrPreStartErrorSink.reportRuntimeTaskException('post-commit actor binding', error),
      actorHandoff: this.actorHandoff,
      actorTransferRuntime: this.actorTransferRuntime,
      actorTransferRegistry: this.actorTransferRegistry,
      shutdownSignal: () => this.state?.abortController.signal,
      metrics: this.metrics,
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
    return new ZLinkSpotNodeRuntimeOptionsFactory({
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
      this.diagnosticsContext()
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

  requirePrimarySpotNode(): ZLinkBackendSpotNode {
    const node = this.spotNodeRuntime?.primaryNode;
    if (node === undefined) {
      throw new Error('Primary Entry Spot node is not started.');
    }
    return node;
  }

  private ensureLocationRuntime(): ZLinkLocationRuntime | undefined {
    return this.locationOwner.ensureRuntime(this.meshRouters.primarySpotMeshName());
  }

  private createLocationSpotRouteResolver(): ZLinkSpotRouteResolver | undefined {
    this.ensureLocationRuntime();
    return this.locationOwner.createSpotRouteResolver(
      this.meshRouters.spotLocationMeshNames(),
      this.meshRouters.spotRouterChannelIdByMesh()
    );
  }

  private createActorLocationResolver(): ZLinkStoreLocationResolvers | undefined {
    return this.locationOwner.createActorLocationResolver();
  }

}

function drainMetricState(state: import('../../contracts').ZLinkDrainState): string {
  switch (state) {
    case 'Serving': return 'serving';
    case 'Draining': return 'draining';
    case 'Drained': return 'drained';
    case 'ForceStopping': return 'force_stopping';
  }
}

class ZLinkDrainingStatePublishError extends Error {
  constructor(cause: unknown) {
    super('Failed to publish draining peer rows.', { cause });
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

function waitForDrain(operation: Promise<ZLinkDrainResult>, signal?: AbortSignal): Promise<ZLinkDrainResult> {
  if (signal?.aborted === true) return Promise.reject(signal.reason ?? new Error('Drain wait was aborted.'));
  if (signal === undefined) return operation;
  return new Promise((resolve, reject) => {
    const aborted = () => reject(signal.reason ?? new Error('Drain wait was aborted.'));
    signal.addEventListener('abort', aborted, { once: true });
    operation.then(resolve, reject).finally(() => signal.removeEventListener('abort', aborted)).catch(() => undefined);
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
