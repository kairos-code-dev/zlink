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
  ZLinkRuntimeEventPublisher
} from '../../contracts';
import type { ZLinkSpotRouteResolver } from '../spots/spot-routing-internal';
import { ZLinkMessageFlowLogMode } from '../../contracts';
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
  type ZLinkDiagnosticsContext,
  type ZLinkMessageFlowModeCell
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
  type ZLinkStreamPayloadCodec,
  ZLinkStreamBindingRuntime,
  ZLinkStreamRuntimeManager
} from '../streams';
import type {
  ZLinkLocationRuntime,
  ZLinkStoreLocationResolvers
} from '../locations';
import type { IZLinkLocationRuntimeQuery } from '../../contracts/Locations';
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
  readonly locationRuntimeQuery?: IZLinkLocationRuntimeQuery;
  start(): Promise<void>;
  stop(): Promise<void>;
}

export interface ZLinkFrameworkRuntimeHostOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly lifecycleSink?: string[];
  readonly providerResolver?: ZLinkProviderResolver;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
}

export class ZLinkFrameworkRuntimeHost implements ZLinkFrameworkRuntime, ZLinkMessageFlowControl {
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
    this.meshRouters = new MeshRouterResolver(options.registration);
    this.locationOwner = new ZLinkLocationRuntimeOwner({
      registration: options.registration,
      runtimeEventPublisher: this.runtimeEventPublisher,
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
        return factory(actorId);
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
      shutdownSignal: () => this.state?.abortController.signal
    });
  }

  get isStarted(): boolean {
    return this.state !== undefined;
  }

  get locationRuntimeQuery(): IZLinkLocationRuntimeQuery | undefined {
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
        this.options.registration.dispatch?.diagnostics?.messageFlowLogMode ??
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
        dispatchErrors
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
    await this.stop();
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

  createLocationRefResolver(): ZLinkStoreLocationResolvers | undefined {
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
      detachedTaskRunner: this.detachedTaskRunner()
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
      shutdownSignal: () => this.state?.abortController.signal
    });
  }

  private createChannelRuntimeOptions() {
    return new ZLinkChannelRuntimeOptionsFactory({
      monitoringAdapter: this.backendAdapterFactory.createMonitoringAdapter(),
      messageFlowModeCell: this.messageFlowModeCell,
      boundSessionRelay: this.boundSessionRelay,
      spotManager: () => this.spotManager
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
      boundSessionRelay: this.boundSessionRelay,
      actorHandoff: this.actorHandoff,
      detachedTaskRunner: this.detachedTaskRunner()
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
