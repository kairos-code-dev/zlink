import { randomUUID } from 'node:crypto';
import { ZLinkNodeBackendAdapterFactory } from '../backend';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendAdapterFactory,
  ZLinkBackendContext,
  ZLinkBackendSocketMonitor,
  ZLinkMonitoringBackendAdapter,
  ZLinkBackendSpotNode
} from '../backend';
import { ZLinkConfigurationException } from '../configuration';
import type { ZLinkFrameworkRegistration } from '../configuration';
import type {
  ActorRef,
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkActorJoinResult,
  ZLinkBoundSession,
  ZLinkBoundSessionSendCall,
  ZLinkProviderResolver,
  ZLinkRouteRequestContext,
  ZLinkRuntimeEventPublisher,
  ZLinkSessionActor,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotRemoteAddressResolver
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { ZLinkMessageFlowLogMode, ZLinkSpotKind } from '../../contracts';
import type { ZLinkMessageFlowControl } from '../../contracts';
import {
  DefaultZLinkChannelClient,
  DefaultZLinkFanoutClient,
  DefaultZLinkSpotPublisherClient,
  DefaultZLinkChannelRuntimeOptions,
  ZLinkDispatchErrorReporter,
  ZLinkChannelRuntimeManager,
  ZLinkRuntimeChannelTransport,
  ZLinkRuntimeRouteTransport
} from '../channels';
import {
  ZLinkFrameworkRuntimeState,
  ZLinkRuntimeErrorSink
} from '../execution';
import {
  createDiagnosticsContext,
  DefaultZLinkRuntimeEventPublisher,
  ZLinkLocationMonitoringEventEmitter,
  ZLinkLocationRuntimeMonitoringSource,
  ZLinkSocketMonitoringSource,
  ZLinkSpotMonitoringSource,
  type ZLinkMessageFlowModeCell
} from '../diagnostics';
import { DefaultZLinkSpotManager, ZLinkRuntimeSpotPublisherTransport, ZLinkSpotNodeRuntimeManager } from '../spots';
import type {
  DefaultZLinkActorManager,
  ZLinkActorJoinCoordinator,
  ZLinkActorManagerOptions,
  ZLinkRemoteActorPacketTarget,
  ZLinkRemoteBoundSessionTarget,
  ZLinkActorRuntimeState,
  ZLinkActorRoutedJoinTransport
} from '../actors';
import {
  ZLinkActorNativeJoinCoordinator,
  ZLINK_REMOTE_ACTOR_JOIN_PACKET,
  ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET,
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET
} from '../actors';
import {
  DefaultZLinkBoundSessionFactory,
  type ZLinkBoundSessionResponseTarget,
  type ZLinkStreamPayloadCodec,
  ZLinkStreamBindingRuntime,
  ZLinkStreamRuntimeManager
} from '../streams';
import {
  ZLinkInMemoryLocationStore,
  ZLinkLocationSpotRemoteAddressResolver,
  ZLinkLocationLifecycle,
  ZLinkLocationRuntime,
  ZLinkOwnerLeaseTracker,
  ZLinkStoreLocationResolvers,
  type ZLinkLocationEventSink,
  type ZLinkLocationRuntimeStores
} from '../locations';
import { Message as BindingMessage } from '@zlink-systems/zlink';
import {
  decodeStreamHeader,
  encodeStreamHeader,
  messageToBytes,
  type ZLinkStreamFrameHeader,
  ZLinkStreamCodec,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind
} from '../streams/protocol';
import { wrapFrameworkPayloadMessage } from '../messaging/payload-codec';
import {
  decodeRemoteActorPacketTarget,
  encodeRemoteActorPacketTarget,
  normalizeRuntimeRoutingId
} from '../spots/route-wire-codec';
import type { IZLinkLocationRuntimeQuery } from '../../contracts/Locations';

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
  private locationStores?: ZLinkLocationRuntimeStores;
  private locationRuntime?: ZLinkLocationRuntime;
  private locationLifecycle?: ZLinkLocationLifecycle;
  private locationEvents?: ZLinkLocationEventSink;
  private readonly locationFallbackNodeRid: RoutingId;
  private actorManager?: DefaultZLinkActorManager;
  private spotManager?: DefaultZLinkSpotManager;
  private readonly destroyedActorRefs = new Map<string, ActorRef>();
  private readonly runtimeEventPublisher: ZLinkRuntimeEventPublisher;
  // Shared, runtime-mutable message-flow mode cell — installed once so
  // setMessageFlowMode flips every surface live. Seeded from config at start().
  private readonly messageFlowModeCell: ZLinkMessageFlowModeCell = {
    mode: ZLinkMessageFlowLogMode.ErrorsOnly
  };
  private readonly preStartErrorSink = new ZLinkRuntimeErrorSink();
  readonly channelTransport = new ZLinkRuntimeChannelTransport(() => this.channelRuntime);
  readonly channelRuntimeOptions = new DefaultZLinkChannelRuntimeOptions(() => this.channelRuntime);
  readonly routeTransport = new ZLinkRuntimeRouteTransport(
    () => this.channelRuntime,
    (routerChannelId) => this.canUseRouterChannel(routerChannelId)
  );
  readonly spotPublisherTransport = new ZLinkRuntimeSpotPublisherTransport(() => this.spotNodeRuntime);
  readonly streamBindingRuntime: ZLinkStreamBindingRuntime;
  readonly boundSessionFactory: DefaultZLinkBoundSessionFactory;
  private readonly sessionActorPacketTargets = new WeakMap<ZLinkSessionActor, ZLinkRemoteActorPacketTarget>();
  private readonly sessionActorPacketTargetsByActor = new Map<string, ZLinkRemoteActorPacketTarget>();
  private readonly sessionActorPacketTargetsByActorId = new Map<string, ZLinkRemoteActorPacketTarget>();

  constructor(readonly options: ZLinkFrameworkRuntimeHostOptions, internalOptions?: unknown) {
    this.backendAdapterFactory = resolveBackendAdapterFactory(internalOptions);
    this.lifecycleSink = options.lifecycleSink;
    this.runtimeEventPublisher = options.runtimeEventPublisher ?? new DefaultZLinkRuntimeEventPublisher();
    this.locationFallbackNodeRid = `node-${randomUUID()}`;
    this.streamBindingRuntime = new ZLinkStreamBindingRuntime({
      streamPayloadCodec: resolveStreamPayloadCodec(options.registration),
      streamCompression: options.registration.streamCompression,
      messageSerializers: options.registration.messageSerializers,
      nativeActorNodeProvider: () => this.spotNodeRuntime?.primaryNode,
      relay: (actor, header, payload, signal) =>
        this.relayActorPacket(actor, header, payload, signal),
      notifyDisconnected: (actor, signal) =>
        this.notifyBoundActorDisconnected(actor, signal)
    });
    this.boundSessionFactory = new DefaultZLinkBoundSessionFactory(this.streamBindingRuntime);
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
      const dispatchErrors = new ZLinkDispatchErrorReporter(
        undefined,
        undefined,
        this.state.errorSink,
        createDiagnosticsContext(
          this.options.registration.dispatch,
          this.options.providerResolver,
          this.messageFlowModeCell
        )
      );
      channelRuntime = new ZLinkChannelRuntimeManager(
        this.options.registration,
        channelAdapter,
        context,
        this.options.providerResolver,
        {
          messageFlowModeCell: this.messageFlowModeCell,
          internalRouteSendHandlers: new Map([
            [ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET, {
              handle: async (payload) => {
                await this.receiveRemoteBoundSessionSend(payload);
              }
            }],
            [ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET, {
              handle: async (payload) => {
                await this.receiveRemoteBoundSessionResponse(payload);
              }
            }],
            [ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET, {
              handle: async (payload) => {
                await this.receiveRemoteBoundSessionError(payload);
              }
            }]
          ]),
          internalRouteRequestHandlers: new Map([
            [ZLINK_REMOTE_ACTOR_JOIN_PACKET, {
              handle: (payload, routeContext) =>
                this.receiveRemoteActorJoin(payload, routeContext)
            }],
            [ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET, {
              handle: (payload, routeContext) =>
                this.receiveRemoteActorPacketRelay(payload, routeContext)
            }]
          ]),
          localSpotRouteDispatcher: {
            send: async (spotRid, packetName, message, routeContext) => {
              if (this.spotManager === undefined) {
                throw new Error('Local SPOT route dispatch requires ZLINK_SPOT_MANAGER.');
              }
              await this.spotManager.dispatchRoutedSpotSend(spotRid, packetName, message, routeContext);
            },
            request: async (spotRid, packetName, request, routeContext) => {
              if (this.spotManager === undefined) {
                throw new Error('Local SPOT route dispatch requires ZLINK_SPOT_MANAGER.');
              }
              return await this.spotManager.dispatchRoutedSpotRequest(spotRid, packetName, request, routeContext);
            }
          }
        }
      );
      this.channelRuntime = channelRuntime;
      spotNodeRuntime = new ZLinkSpotNodeRuntimeManager({
        registration: this.options.registration,
        backendAdapterFactory: this.backendAdapterFactory,
        context,
        channelClient: new DefaultZLinkChannelClient(this.options.registration, this.channelTransport),
        fanoutClient: new DefaultZLinkFanoutClient(this.options.registration, this.channelTransport),
        spotPublisherClient: new DefaultZLinkSpotPublisherClient(this.options.registration, this.spotPublisherTransport),
        providerResolver: this.options.providerResolver,
        dispatchErrors,
        runtimeEventPublisher: this.runtimeEventPublisher,
        messageSerializers: this.options.registration.messageSerializers,
        actorResolver: (actorId) => this.actorManager?.getState(actorId)?.actor,
        entryActorCommitter: async (actor) => {
          const state = this.actorManager?.getState(actor.actorId);
          const entryNode = this.spotNodeRuntime?.primaryNode;
          if (state === undefined || entryNode === undefined) {
            return;
          }
          const generation = state.nativeActorRef?.generation ?? 0n;
          state.clearJoinedSpot();
          this.clearRemoteActorPacketTarget(actor.actorId);
          const actorRef = {
            nodeRid: entryNode.routingId,
            actorId: actor.actorId,
            generation
          } as ZLinkBackendActorRef;
          state.setNativeActorRef(actorRef);
          await this.streamBindingRuntime.refreshActor(actorRef as ActorRef);
        },
        routedBoundSessionReceiver: (actorId, message, packetName, metadata) =>
          this.receiveRoutedBoundSession(actorId, message, packetName, metadata),
        routedBoundSessionResponseReceiver: (actorId, packetName, requestSeq, message, metadata, actorPacketTarget) =>
          this.receiveRoutedBoundSessionResponse(actorId, packetName, requestSeq, message, metadata, actorPacketTarget),
        routedBoundSessionErrorReceiver: (actorId, packetName, requestSeq, error, metadata, actorPacketTarget) =>
          this.receiveRoutedBoundSessionError(actorId, packetName, requestSeq, error, metadata, actorPacketTarget),
        remoteActorPacketTargetReceiver: (actorId, target) => {
          const state = this.actorManager?.getState(actorId);
          state?.setRemoteBoundSessionTarget(target);
        },
        actorPacketTargetProvider: (actorId) => this.actorPacketTargetForState(actorId),
        localActorPacketRouter: async (actorId, parts, returnResponse, remoteBoundSessionTarget) => {
          const spotRid = this.actorManager?.getState(actorId)?.spotRid;
          if (spotRid === undefined || this.spotManager === undefined) {
            return { handled: false };
          }
          return {
            handled: true,
            response: await this.spotManager.dispatchRoutedActorPacket(
              spotRid,
              actorId,
              parts,
              returnResponse,
              remoteBoundSessionTarget
            )
          };
        },
        actorResponseSender: (actor, packetName, requestSeq, response, metadata, signal) =>
          this.sendActorResponse(actor, packetName, requestSeq, response, metadata, signal),
        actorErrorSender: (actorId, packetName, requestSeq, error, metadata, actorRef, signal) =>
          this.sendActorError(actorId, packetName, requestSeq, error, metadata, actorRef, signal),
        actorDestroyer: (node, entryNodeRid, actor, signal) => {
          if (this.actorManager === undefined) {
            throw new Error('Entry Spot actor destroy requires ZLINK_ACTOR_MANAGER.');
          }
          return this.actorManager.destroyActor(node, entryNodeRid, actor, signal);
        }
      });
      await spotNodeRuntime.start();
      this.ensureLocationRuntime();
      if (this.locationRuntime !== undefined) {
        await this.locationRuntime.start(this.locationOwnerNodeRid(spotNodeRuntime));
        startedLocationRuntime = this.locationRuntime;
        if (this.locationStores !== undefined) {
          spotNodeRuntime.configureLocationAutoConnect(
            this.locationRuntime,
            this.locationStores,
            this.options.registration.locations.options,
            this.locationEvents
          );
          await spotNodeRuntime.startLocationAutoConnect();
          channelRuntime.configureLocationAutoConnect(
            this.locationRuntime,
            this.locationStores,
            this.options.registration.locations.options,
            this.locationEvents
          );
          await channelRuntime.startLocationAutoConnect();
        }
      }
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
          locationRuntime: this.locationRuntime,
          monitoringAdapter: this.backendAdapterFactory.createMonitoringAdapter(),
          publisher: this.runtimeEventPublisher
        });
        this.monitoringRuntime.start(this.state);
      }
      this.lifecycleSink?.push('framework:started');
    } catch (error) {
      await Promise.allSettled([
        startedLocationRuntime?.stop(),
        this.monitoringRuntime?.dispose(),
        streamRuntime?.dispose(),
        spotNodeRuntime?.dispose(),
        channelRuntime?.dispose(),
        context.dispose()
      ]);
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
    const locationRuntime = this.locationRuntime;
    const locationLifecycle = this.locationLifecycle;
    this.state = undefined;
    this.channelRuntime = undefined;
    this.spotNodeRuntime = undefined;
    this.streamRuntime = undefined;
    this.monitoringRuntime = undefined;
    this.locationRuntime = undefined;
    this.locationLifecycle = undefined;
    this.locationStores = undefined;
    this.locationEvents = undefined;
    this.lifecycleSink?.push('framework:stop');
    state.abortController.abort();
    await monitoringRuntime?.dispose();
    await streamRuntime?.dispose();
    await spotNodeRuntime?.dispose();
    await channelRuntime?.dispose();
    locationLifecycle?.dispose();
    await locationRuntime?.stop();
    (state.context as { shutdown?: () => void }).shutdown?.();
    await Promise.allSettled(state.listenerTasks);
    await new Promise<void>((resolve) => setImmediate(resolve));
    await state.dispose();
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

  createActorManagerOptions(remoteAddressResolver?: ZLinkSpotRemoteAddressResolver): Pick<
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
    this.ensureLocationRuntime();
    const effectiveRemoteAddressResolver = remoteAddressResolver ?? this.createLocationSpotRemoteAddressResolver();
    return {
      joinCoordinator: new ZLinkLocalFirstActorJoinCoordinator({
        localSpotManager: () => this.spotManager,
        nativeNode: () => this.requirePrimarySpotNode(),
        actorBinder: (actorRef, signal, force) => force === true
          ? this.streamBindingRuntime.refreshActor(actorRef, signal)
          : this.streamBindingRuntime.rebindActor(actorRef, signal),
        native: new ZLinkLazyNativeJoinCoordinator(
          () => this.requirePrimarySpotNode(),
          effectiveRemoteAddressResolver,
          this.routeTransport,
          (actorRef, signal, force) => force === true
            ? this.streamBindingRuntime.refreshActor(actorRef, signal)
            : this.streamBindingRuntime.rebindActor(actorRef, signal),
          () => this.locationLifecycle
        )
      }),
      messageSerializers: this.options.registration.messageSerializers,
      nativeActorNodeProvider: () => this.spotNodeRuntime?.primaryNode,
      locationLifecycle: this.locationLifecycle,
      boundSessionFactory: (actorId) => new ZLinkNativeFallbackBoundSession(
        this.streamBindingRuntime,
        this.routeTransport,
        () => this.requirePrimarySpotNode(),
        () => this.actorManager?.getState(actorId)?.nativeActorRef as ActorRef | undefined,
        () => this.actorManager?.getState(actorId)?.remoteBoundSessionTarget,
        () => this.actorManager?.getState(actorId)?.remoteActorPacketTarget,
        this.options.registration.requestTimeoutMs,
        actorId
      ),
      actorCreatedNodeRidProvider: () => this.spotNodeRuntime?.primaryNode?.routingId,
      actorCreatedNotifier: (nodeRid, actor, createRequest, signal) => {
        this.destroyedActorRefs.delete(actor.actorId);
        return this.spotNodeRuntime?.notifyEntrySpotActorCreated(nodeRid, actor, createRequest, signal) ?? Promise.resolve();
      },
      actorDestroyedCleanup: (actorId) => {
        const actorRef = this.actorManager?.getState(actorId)?.nativeActorRef as ActorRef | undefined;
        if (actorRef !== undefined) {
          this.destroyedActorRefs.set(actorId, actorRef);
        }
        this.streamBindingRuntime.unbindActor(actorId);
      }
    };
  }

  createSpotManagerOptions(): object {
    this.ensureLocationRuntime();
    return {
      nodeRid: undefined,
      nodeRidProvider: () => this.spotNodeRuntime?.primaryNode?.routingId,
      entryNodeRid: undefined,
      entryNodeRidProvider: () => this.spotNodeRuntime?.primaryNode?.routingId,
      actorEntryNodeRidProvider: (actor: ZLinkActor) =>
        this.actorManager?.getState(actor.actorId)?.nativeActorRef?.nodeRid as RoutingId | undefined,
      entrySpotCallbacks: {
        onJoinedActor: (actor: ZLinkActor, signal?: AbortSignal) =>
          this.spotNodeRuntime?.notifyPrimaryEntrySpotActorJoined(actor, signal) ?? Promise.resolve(),
        onLeaveActor: (actor: ZLinkActor, signal?: AbortSignal) =>
          this.spotNodeRuntime?.notifyPrimaryEntrySpotActorLeft(actor, signal) ?? Promise.resolve()
      },
      channelClient: new DefaultZLinkChannelClient(this.options.registration, this.channelTransport),
      fanoutClient: new DefaultZLinkFanoutClient(this.options.registration, this.channelTransport),
      spotPublisherClient: new DefaultZLinkSpotPublisherClient(this.options.registration, this.spotPublisherTransport),
      messageSerializers: this.options.registration.messageSerializers,
      runtimeEventPublisher: this.runtimeEventPublisher,
      locationLifecycle: this.locationLifecycle,
      locationMeshName: this.primarySpotMeshName(),
      remoteAddressResolver: this.createLocationSpotRemoteAddressResolver(),
      createNativeSpot: (spotRid: RoutingId) => this.spotNodeRuntime?.primaryNode?.getOrCreateSpot(spotRid).spot,
      nativeSpotNodeProvider: () => this.spotNodeRuntime?.primaryNode,
      actorResolver: (actorId: string) => this.actorManager?.getState(actorId)?.actor,
      routedActorProvider: async (
        actorId: string,
        actorType: string,
        actorRef?: ActorRef,
        remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
        actorCreateRequest?: Message,
        signal?: AbortSignal
      ) => {
        if (this.actorManager === undefined) {
          throw new Error('Routed actor join requires ZLINK_ACTOR_MANAGER.');
        }
        const actor = actorRef === undefined
          ? await this.actorManager.getOrCreateActor(actorId, actorType, signal)
          : await this.actorManager.getOrCreateWithNativeRef(
              actorId,
              actorType,
              actorRef as unknown as ZLinkBackendActorRef,
              actorCreateRequest === undefined
                ? undefined
                : wrapFrameworkPayloadMessage(actorCreateRequest, this.options.registration.messageSerializers),
              signal
            );
        const state = this.actorManager.getState(actorId);
        if (state === undefined) {
          throw new Error(`Actor '${actorId}' state was not created.`);
        }
        if (actorRef !== undefined) {
          state.setNativeActorRef(actorRef as unknown as ZLinkBackendActorRef);
          state.setRemoteBoundSessionTarget(remoteBoundSessionTarget);
          return { actor, actorRef: actorRef as unknown as ZLinkBackendActorRef };
        }
        const localActorRef = state.ensureNativeActorRef(this.requirePrimarySpotNode());
        return { actor, actorRef: localActorRef };
      },
      nativeJoinBoundSessionTargetResolver: (info: { sourceActor: ActorRef; sourceSpotRid?: RoutingId }) => {
        const routerChannelId = this.defaultSpotRouterChannelId()
          ?? this.defaultRouterChannelId();
        if (routerChannelId === undefined) {
          return undefined;
        }
        return {
          routerChannelId,
          targetNodeRid: normalizeRuntimeRoutingId(info.sourceActor.nodeRid),
          spotRid: normalizeRuntimeRoutingId(info.sourceSpotRid ?? info.sourceActor.nodeRid)
        };
      },
      routedActorCommitter: (actor: ZLinkActor, spotRid: RoutingId, spot: ZLinkSpot) => {
        this.actorManager?.getState(actor.actorId)?.setJoinedSpot(spotRid, spot);
      },
      routedActorLeaveCommitter: (actor: ZLinkActor) => {
        const state = this.actorManager?.getState(actor.actorId);
        state?.clearJoinedSpot();
        this.clearRemoteActorPacketTarget(actor.actorId);
      },
      routedBoundSessionReceiver: async (
        actorId: string,
        message: unknown,
        packetName: string | undefined,
        metadata: ReadonlyMap<string, string>
      ) => {
        await this.receiveRoutedBoundSession(actorId, message, packetName, metadata);
      },
      routedBoundSessionResponseReceiver: async (
        actorId: string,
        packetName: string,
        requestSeq: bigint,
        message: unknown,
        metadata: ReadonlyMap<string, string>,
        actorPacketTarget?: unknown
      ) => {
        await this.receiveRoutedBoundSessionResponse(actorId, packetName, requestSeq, message, metadata, actorPacketTarget);
      },
      routedBoundSessionErrorReceiver: async (
        actorId: string,
        packetName: string,
        requestSeq: bigint,
        error: unknown,
        metadata: ReadonlyMap<string, string>,
        actorPacketTarget?: unknown
      ) => {
        await this.receiveRoutedBoundSessionError(actorId, packetName, requestSeq, error, metadata, actorPacketTarget);
      },
      remoteActorPacketTargetReceiver: (actorId: string, target: ZLinkRemoteBoundSessionTarget) => {
        const state = this.actorManager?.getState(actorId);
        state?.setRemoteBoundSessionTarget(target);
      },
      actorPacketTargetProvider: (actorId: string) => this.actorPacketTargetForState(actorId),
      actorResponseSender: async (
        actor: ZLinkActor,
        packetName: string,
        requestSeq: bigint,
        response: unknown,
        metadata: ReadonlyMap<string, string>,
        signal?: AbortSignal
      ) => this.sendActorResponse(actor, packetName, requestSeq, response, metadata, signal),
      actorErrorSender: async (
        actorId: string,
        packetName: string,
        requestSeq: bigint,
        error: unknown,
        metadata: ReadonlyMap<string, string>,
        actorRef?: ActorRef,
        signal?: AbortSignal
      ) => this.sendActorError(actorId, packetName, requestSeq, error, metadata, actorRef, signal),
      dispatchErrors: new ZLinkDispatchErrorReporter(
        undefined,
        undefined,
        {
          reportRuntimeTaskException: (taskName: string, error: unknown) =>
            (this.errorSink ?? this.preStartErrorSink).reportRuntimeTaskException(taskName, error)
        },
        createDiagnosticsContext(
          this.options.registration.dispatch,
          this.options.providerResolver,
          this.messageFlowModeCell
        )
      )
    };
  }

  requirePrimarySpotNode(): ZLinkBackendSpotNode {
    const node = this.spotNodeRuntime?.primaryNode;
    if (node === undefined) {
      throw new Error('Primary Entry Spot node is not started.');
    }
    return node;
  }

  private requireSpotNodeRuntime(): ZLinkSpotNodeRuntimeManager {
    const runtime = this.spotNodeRuntime;
    if (runtime === undefined) {
      throw new Error('SPOT node runtime is not started.');
    }
    return runtime;
  }

  private requireSpotManager(): DefaultZLinkSpotManager {
    const manager = this.spotManager;
    if (manager === undefined) {
      throw new Error('SPOT manager runtime is not started.');
    }
    return manager;
  }

  private createLocationRuntimeStores(): ZLinkLocationRuntimeStores | undefined {
    const locations = this.options.registration.locations;
    if (locations.useInMemoryStores) {
      const store = new ZLinkInMemoryLocationStore();
      return {
        locationStore: store,
        peerStore: store,
        spotStore: store,
        actorStore: store,
        routeStore: store,
        ownerLeaseStore: store
      };
    }
    const store = locations.storeInstance;
    if (store !== undefined) {
      return {
        locationStore: store,
        peerStore: store,
        spotStore: store,
        actorStore: store,
        routeStore: store,
        ownerLeaseStore: store
      };
    }
    return undefined;
  }

  private ensureLocationRuntime(): ZLinkLocationRuntime | undefined {
    if (this.locationRuntime !== undefined) {
      return this.locationRuntime;
    }
    const stores = this.locationStores ?? this.createLocationRuntimeStores();
    if (stores === undefined) {
      return undefined;
    }
    this.locationStores = stores;
    const runtime = new ZLinkLocationRuntime({
      stores,
      options: this.options.registration.locations.options,
      events: this.ensureLocationEvents()
    });
    this.locationRuntime = runtime;
    this.locationLifecycle = new ZLinkLocationLifecycle(runtime, stores.actorStore, this.primarySpotMeshName() ?? '');
    return runtime;
  }

  private createLocationSpotRemoteAddressResolver(): ZLinkSpotRemoteAddressResolver | undefined {
    const stores = this.locationStores;
    if (stores === undefined) {
      return undefined;
    }
    const meshNames = this.spotLocationMeshNames();
    if (meshNames.length === 0) {
      return undefined;
    }
    const leaseTracker = new ZLinkOwnerLeaseTracker({
      store: stores.ownerLeaseStore,
      options: this.options.registration.locations.options
    });
    return new ZLinkLocationSpotRemoteAddressResolver(
      new ZLinkStoreLocationResolvers({
        stores,
        leaseTracker,
        events: this.locationEvents
      }),
      meshNames,
      this.spotRouterChannelIdByMesh()
    );
  }

  private ensureLocationEvents(): ZLinkLocationEventSink | undefined {
    if (this.locationEvents !== undefined) {
      return this.locationEvents;
    }
    const monitoring = this.options.registration.monitoring;
    if (monitoring === undefined) {
      return undefined;
    }
    const emitter = new ZLinkLocationMonitoringEventEmitter({
      peer: monitoring.locationPeer?.[0],
      spot: monitoring.locationSpot?.[0],
      actor: monitoring.locationActor?.[0],
      route: monitoring.locationRoute?.[0]
    }, this.runtimeEventPublisher);
    this.locationEvents = emitter;
    return emitter;
  }

  private locationOwnerNodeRid(spotNodeRuntime?: ZLinkSpotNodeRuntimeManager): RoutingId {
    const spotNodeRid = spotNodeRuntime?.primaryNode?.routingId;
    if (spotNodeRid !== undefined) {
      return spotNodeRid;
    }
    for (const channel of this.options.registration.channels.values()) {
      if (channel.server?.routingId !== undefined) {
        return channel.server.routingId;
      }
    }
    for (const routeChannel of this.options.registration.routeChannelOptions.values()) {
      if (routeChannel.routingId !== undefined) {
        return routeChannel.routingId;
      }
    }
    for (const spotNode of this.options.registration.spotNodes.values()) {
      const routingId = spotNode.routingId ?? spotNode.router?.routingId ?? spotNode.pubSub?.routingId;
      if (routingId !== undefined) {
        return routingId;
      }
    }
    return this.locationFallbackNodeRid;
  }

  private requireActorManager(): DefaultZLinkActorManager {
    const manager = this.actorManager;
    if (manager === undefined) {
      throw new Error('Actor manager runtime is not started.');
    }
    return manager;
  }

  private async receiveRoutedBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>
  ): Promise<void> {
    const sent = this.streamBindingRuntime.sendLocalBoundSession(actorId, message, packetName, metadata);
    if (sent) {
      return;
    }
    const boundSessionFactory = this.createActorManagerOptions().boundSessionFactory;
    if (boundSessionFactory === undefined) {
      throw new Error('Bound session factory is not configured.');
    }
    const call = boundSessionFactory(actorId).send(message);
    if (packetName !== undefined) {
      call.packetName(packetName);
    }
    for (const [key, value] of metadata) {
      call.metadata(key, value);
    }
    call.submit();
  }

  private async receiveRoutedBoundSessionResponse(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    metadata: ReadonlyMap<string, string>,
    actorPacketTarget?: unknown
  ): Promise<void> {
    this.updateRemoteActorPacketTarget(actorId, actorPacketTarget);
    const sent = this.streamBindingRuntime.sendLocalBoundSessionResponse(
      actorId,
      packetName,
      requestSeq,
      message,
      metadata
    );
    if (!sent) {
      return;
    }
  }

  private async receiveRoutedBoundSessionError(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    actorPacketTarget?: unknown
  ): Promise<void> {
    this.updateRemoteActorPacketTarget(actorId, actorPacketTarget);
    const sent = this.streamBindingRuntime.sendLocalBoundSessionError(
      actorId,
      packetName,
      requestSeq,
      error,
      metadata
    );
    if (!sent) {
      return;
    }
  }

  private async receiveRemoteBoundSessionSend(payload: unknown): Promise<{ readonly ok: boolean }> {
    const send = decodeRemoteBoundSessionSendPayload(payload);
    const metadata = new Map(Object.entries(send.metadata ?? {}));
    if (this.streamBindingRuntime.sendLocalBoundSession(
      send.actorId,
      send.message,
      send.boundPacketName,
      metadata
    )) {
      return { ok: true };
    }
    const boundSessionFactory = this.createActorManagerOptions().boundSessionFactory;
    if (boundSessionFactory === undefined) {
      throw new Error('Bound session factory is not configured.');
    }
    const call = boundSessionFactory(send.actorId).send(send.message);
    if (send.boundPacketName !== undefined) {
      call.packetName(send.boundPacketName);
    }
    for (const [key, value] of metadata) {
      call.metadata(key, value);
    }
    call.submit();
    return { ok: true };
  }

  private async receiveRemoteBoundSessionResponse(payload: unknown): Promise<{ readonly ok: boolean }> {
    const response = decodeRemoteBoundSessionResponsePayload(payload);
    this.updateRemoteActorPacketTarget(response.actorId, response.actorPacketTarget);
    const sent = this.streamBindingRuntime.sendLocalBoundSessionResponse(
      response.actorId,
      response.boundPacketName,
      response.requestSeq,
      response.message,
      new Map(Object.entries(response.metadata ?? {}))
    );
    if (!sent) {
      return { ok: false };
    }
    return { ok: true };
  }

  private async receiveRemoteBoundSessionError(payload: unknown): Promise<{ readonly ok: boolean }> {
    const response = decodeRemoteBoundSessionErrorPayload(payload);
    this.updateRemoteActorPacketTarget(response.actorId, response.actorPacketTarget);
    const sent = this.streamBindingRuntime.sendLocalBoundSessionError(
      response.actorId,
      response.boundPacketName,
      response.requestSeq,
      response.error,
      new Map(Object.entries(response.metadata ?? {}))
    );
    if (!sent) {
      return { ok: false };
    }
    return { ok: true };
  }

  private async sendRemoteBoundSessionResponse(
    target: ZLinkRemoteBoundSessionTarget,
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    const actorPacketTarget = encodeRemoteActorPacketTarget(
      this.actorPacketTargetForState(actorId, target.routerChannelId)
    );
    await this.sendRemoteBoundSessionControl(target, {
      packetName: ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET,
      actorId,
      boundPacketName: packetName,
      requestSeq: requestSeq.toString(),
      message,
      metadata: Object.fromEntries(metadata),
      actorPacketTarget
    }, signal);
  }

  private async sendRemoteBoundSessionError(
    target: ZLinkRemoteBoundSessionTarget,
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    await this.sendRemoteBoundSessionControl(target, {
      packetName: ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET,
      actorId,
      boundPacketName: packetName,
      requestSeq: requestSeq.toString(),
      error: error instanceof Error
        ? { code: error.constructor.name, message: error.message }
        : error,
      metadata: Object.fromEntries(metadata),
      actorPacketTarget: encodeRemoteActorPacketTarget(
        this.actorPacketTargetForState(actorId, target.routerChannelId)
      )
    }, signal);
  }

  private updateRemoteActorPacketTarget(actorId: string, value: unknown): void {
    const actorPacketTarget = decodeRemoteActorPacketTarget(value);
    const state = this.actorManager?.getState(actorId);
    if (actorPacketTarget !== undefined) {
      if (typeof state?.setRemoteActorPacketTarget === 'function') {
        state.setRemoteActorPacketTarget(actorPacketTarget);
      }
      this.sessionActorPacketTargetsByActorId.set(actorId, actorPacketTarget);
      return;
    }
    if (typeof state?.setRemoteActorPacketTarget === 'function') {
      state.setRemoteActorPacketTarget(undefined);
    }
    this.sessionActorPacketTargetsByActorId.delete(actorId);
  }

  private async sendRemoteBoundSessionControl(
    target: ZLinkRemoteBoundSessionTarget,
    payload: Record<string, unknown>,
    signal?: AbortSignal
  ): Promise<void> {
    const rawPayload = BindingMessage.from(Buffer.from(JSON.stringify(payload)));
    try {
      const replyParts = await this.routeTransport.requestRawToSpot(
        {
          routerChannelId: target.routerChannelId,
          targetNodeRid: target.targetNodeRid,
          spotRid: target.spotRid,
          spotKind: ZLinkSpotKind.Entry
        },
        rawPayload,
        {
          timeoutMs: this.options.registration.requestTimeoutMs,
          signal
        }
      );
      for (const part of replyParts) {
        part.close();
      }
    } finally {
      rawPayload.close();
    }
  }

  private async receiveRemoteActorJoin(
    payload: unknown,
    routeContext: ZLinkRouteRequestContext
  ): Promise<{
    readonly accepted: boolean;
    readonly actorNodeRid: string;
    readonly actorNodeRidHex?: string;
    readonly actorId: string;
    readonly actorGeneration: string;
    readonly reply?: string;
  }> {
    const join = decodeRemoteActorJoinPayload(payload);
    const actorManager = this.requireActorManager();
    const actor = await actorManager.getOrCreateActor(join.actorId, join.actorType);
    const state = actorManager.getState(join.actorId);
    if (state === undefined) {
      throw new Error(`Actor '${join.actorId}' state was not created.`);
    }
    const actorRef = {
      nodeRid: normalizeRuntimeRoutingId(join.actorNodeRid),
      actorId: join.actorId,
      generation: BigInt(join.actorGeneration)
    };
    state.setNativeActorRef(actorRef as unknown as ZLinkBackendActorRef);
    state.setRemoteBoundSessionTarget({
      routerChannelId: join.boundSessionRouterChannelId ?? join.routerChannelId ?? routeContext.channelName ?? '',
      targetNodeRid: normalizeRuntimeRoutingId(join.boundSessionTargetNodeRid ?? routeContext.sourceNodeRid),
      spotRid: normalizeRuntimeRoutingId(join.boundSessionSpotRid ?? join.sourceSpotRid ?? routeContext.sourceNodeRid)
    });
    const request = BindingMessage.from(Buffer.from(join.request, 'base64'));
    try {
      const response = await this.requireSpotManager().admitActorJoin(
        join.spotRid as RoutingId,
        actor,
        request,
        (spot) => state.setJoinedSpot(join.spotRid as RoutingId, spot)
      );
      const reply = response.reply as Message | undefined;
      return {
        accepted: response.accepted,
        actorNodeRid: join.actorNodeRid,
        actorNodeRidHex: join.actorNodeRidHex,
        actorId: join.actorId,
        actorGeneration: join.actorGeneration,
        reply: reply?.data().toString('base64')
      };
    } finally {
      request.close();
    }
  }

  private async sendActorResponse(
    actor: ZLinkActor,
    packetName: string,
    requestSeq: bigint,
    response: unknown,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    const state = this.actorManager?.getState(actor.actorId);
    const actorRef = state?.nativeActorRef as ActorRef | undefined;
    if (actorRef === undefined) {
      throw new Error(`Actor '${actor.actorId}' does not have a native actor ref.`);
    }
    if (this.streamBindingRuntime.sendLocalBoundSessionResponse(
      actor.actorId,
      packetName,
      requestSeq,
      response,
      metadata
    )) {
      return;
    }
    const remoteTarget = state?.remoteBoundSessionTarget;
    if (remoteTarget !== undefined) {
      await this.sendRemoteBoundSessionResponse(
        remoteTarget,
        actor.actorId,
        packetName,
        requestSeq,
        response,
        metadata,
        signal
      );
      return;
    }
    await this.streamBindingRuntime.sendNativeBoundSessionResponse(
      this.requirePrimarySpotNode(),
      actorRef,
      packetName,
      requestSeq,
      response,
      metadata,
      signal
    );
  }

  private async notifyBoundActorDisconnected(
    actor: ZLinkSessionActor,
    signal?: AbortSignal
  ): Promise<void> {
    const state = this.actorManager?.getState(actor.actorId);
    const currentRemoteBoundSessionTarget =
      state?.spotRid === undefined ? undefined : state.remoteBoundSessionTarget;
    const currentRemoteActorPacketTarget =
      state?.spotRid === undefined ? undefined : state.remoteActorPacketTarget;
    const remoteTarget = currentRemoteBoundSessionTarget
      ?? currentRemoteActorPacketTarget
      ?? (state?.spotRid === undefined ? undefined : this.sessionActorPacketTargets.get(actor))
      ?? (state?.spotRid === undefined ? undefined : this.sessionActorPacketTargetsByActor.get(sessionActorPacketTargetKey(actor)))
      ?? this.remoteActorPacketTargetForBoundActor(actor.ref);
    if (remoteTarget !== undefined) {
      await this.notifyRemoteActorDisconnected(actor.actorId, remoteTarget, signal);
      return;
    }
    const actorRefTarget = this.remoteActorPacketTargetForActorRef(actor.ref);
    if (actorRefTarget !== undefined) {
      await this.notifyRemoteActorDisconnected(actor.actorId, actorRefTarget, signal);
      return;
    }
    if (state?.spotRid !== undefined && state.actor !== undefined) {
      const handled = await this.requireSpotManager().notifyJoinedSpotActorDisconnected(
        state.spotRid,
        state.actor,
        signal
      );
      if (handled) {
        return;
      }
    }
    await this.notifyActorDisconnectedById(actor.actorId, signal);
  }

  private clearRemoteActorPacketTarget(actorId: string): void {
    const state = this.actorManager?.getState(actorId);
    state?.setRemoteActorPacketTarget(undefined);
    this.sessionActorPacketTargetsByActorId.delete(actorId);
    const sessionActor = this.streamBindingRuntime.find(actorId);
    if (sessionActor !== undefined) {
      this.sessionActorPacketTargets.delete(sessionActor);
      this.sessionActorPacketTargetsByActor.delete(sessionActorPacketTargetKey(sessionActor));
    }
    const actorRef = state?.nativeActorRef as ActorRef | undefined;
    if (actorRef !== undefined) {
      this.sessionActorPacketTargetsByActor.delete(
        `${String(actorRef.nodeRid)}:${actorId}:${String(actorRef.generation)}`
      );
    }
  }

  private async notifyActorDisconnectedById(actorId: string, signal?: AbortSignal): Promise<void> {
    const state = this.actorManager?.getState(actorId);
    const remoteTarget = state?.remoteBoundSessionTarget ?? state?.remoteActorPacketTarget;
    if (remoteTarget !== undefined) {
      await this.notifyRemoteActorDisconnected(actorId, remoteTarget, signal);
      return;
    }
    await this.notifyLocalActorDisconnectedById(actorId, signal);
  }

  private async notifyLocalActorDisconnectedById(actorId: string, signal?: AbortSignal): Promise<void> {
    const state = this.actorManager?.getState(actorId);
    if (state?.spotRid !== undefined && state.actor !== undefined) {
      const handled = await this.requireSpotManager().notifyJoinedSpotActorDisconnected(
        state.spotRid,
        state.actor,
        signal
      );
      if (handled) {
        return;
      }
    }
    const localActor = state?.actor;
    if (localActor === undefined) {
      throw new Error(`Actor '${actorId}' does not have a local actor instance.`);
    }
    await this.requireSpotNodeRuntime().notifyPrimaryEntrySpotActorDisconnected(localActor, signal);
  }

  private async notifyRemoteActorDisconnected(
    actorId: string,
    remoteTarget: ZLinkRemoteBoundSessionTarget | ZLinkRemoteActorPacketTarget,
    signal?: AbortSignal
  ): Promise<void> {
    const spotKind: ZLinkSpotKind | undefined =
      'spotKind' in remoteTarget ? remoteTarget.spotKind as ZLinkSpotKind | undefined : undefined;
    const header: ZLinkStreamFrameHeader = {
      kind: ZLinkStreamMessageKind.Send,
      codec: ZLinkStreamCodec.Raw,
      flags: ZLinkStreamHeaderFlags.None,
      name: ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET,
      metadata: new Map()
    };
    const payload = {
      packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
      actorId,
      routerChannelId: remoteTarget.routerChannelId,
      header: Buffer.from(encodeStreamHeader(header)).toString('base64'),
      payload: Buffer.alloc(0).toString('base64')
    };
    const address = {
      routerChannelId: remoteTarget.routerChannelId,
      targetNodeRid: remoteTarget.targetNodeRid,
      spotRid: remoteTarget.spotRid,
      spotKind: spotKind ?? ZLinkSpotKind.Entry
    };
    await this.routeTransport.sendToSpot(address, payload, {
      packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
      signal
    });
  }

  private async sendActorError(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    fallbackActorRef?: ActorRef,
    signal?: AbortSignal
  ): Promise<void> {
    if (!this.streamBindingRuntime.sendLocalBoundSessionError(
      actorId,
      packetName,
      requestSeq,
      error,
      metadata
    )) {
      const state = this.actorManager?.getState(actorId);
      const remoteTarget = state?.remoteBoundSessionTarget;
      if (remoteTarget !== undefined) {
        await this.sendRemoteBoundSessionError(
          remoteTarget,
          actorId,
          packetName,
          requestSeq,
          error,
          metadata,
          signal
        );
        return;
      }
      const actorRef = (state?.nativeActorRef as ActorRef | undefined)
        ?? fallbackActorRef
        ?? this.destroyedActorRefs.get(actorId);
      if (actorRef === undefined) {
        throw new Error(`Actor '${actorId}' does not have a native actor ref.`);
      }
      await this.streamBindingRuntime.sendNativeBoundSessionError(
        this.requirePrimarySpotNode(),
        actorRef,
        packetName,
        requestSeq,
        error,
        metadata,
        signal
      );
    }
  }

  private async receiveRemoteActorPacketRelay(
    payload: unknown,
    routeContext: ZLinkRouteRequestContext
  ): Promise<{
    readonly ok: boolean;
    readonly error?: unknown;
    readonly response?: unknown;
    readonly deferredResponse?: boolean;
    readonly actorPacketTarget?: unknown;
  }> {
    const relay = decodeRemoteActorPacketRelayPayload(payload);
    const remoteBoundSessionTarget: ZLinkRemoteBoundSessionTarget | undefined =
      relay.routerChannelId === undefined
        ? undefined
        : {
            routerChannelId: relay.routerChannelId,
            targetNodeRid: normalizeRuntimeRoutingId(relay.boundSessionTargetNodeRid ?? routeContext.sourceNodeRid),
            spotRid: normalizeRuntimeRoutingId(relay.boundSessionSpotRid ?? routeContext.sourceNodeRid)
          };
    const header = BindingMessage.from(Buffer.from(relay.header, 'base64'));
    const body = BindingMessage.from(Buffer.from(relay.payload, 'base64'));
    let closeFrameMessages = true;
    try {
      const frameHeader = decodeStreamHeader(messageToBytes(header));
      if (frameHeader.name === ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET) {
        await this.notifyLocalActorDisconnectedById(relay.actorId);
        return {
          ok: true,
          actorPacketTarget: encodeRemoteActorPacketTarget(
            this.actorPacketTargetForState(relay.actorId, relay.routerChannelId)
          )
        };
      }
      const state = this.actorManager?.getState(relay.actorId);
      if (frameHeader.kind === ZLinkStreamMessageKind.Request && frameHeader.requestSeq !== undefined) {
        const dispatch = state?.spotRid === undefined
          ? this.requireSpotNodeRuntime().dispatchEntryActorPacket(
              relay.actorId,
              [header, body],
              false,
              remoteBoundSessionTarget
            )
          : this.requireSpotManager().dispatchRoutedActorPacket(
              state.spotRid,
              relay.actorId,
              [header, body],
              false,
              remoteBoundSessionTarget
            );
        closeFrameMessages = false;
        void dispatch.catch((error) =>
          (this.errorSink ?? this.preStartErrorSink).reportRuntimeTaskException('remote actor packet relay', error)
        ).finally(() => {
          header.close();
          body.close();
        });
        return {
          ok: true,
          deferredResponse: true,
          actorPacketTarget: encodeRemoteActorPacketTarget(
            this.actorPacketTargetForState(relay.actorId, relay.routerChannelId)
          )
        };
      }
      const response = state?.spotRid === undefined
        ? await this.requireSpotNodeRuntime().dispatchEntryActorPacket(
            relay.actorId,
            [header, body],
            true,
            remoteBoundSessionTarget
          )
        : await this.requireSpotManager().dispatchRoutedActorPacket(
            state.spotRid,
            relay.actorId,
            [header, body],
            true,
            remoteBoundSessionTarget
          );
      return {
        ok: true,
        response,
        actorPacketTarget: encodeRemoteActorPacketTarget(
          this.actorPacketTargetForState(relay.actorId, relay.routerChannelId)
        )
      };
    } catch (error) {
      return {
        ok: false,
        error: error instanceof Error ? error.message : String(error)
      };
    } finally {
      if (closeFrameMessages) {
        header.close();
        body.close();
      }
    }
  }

  private async relayActorPacket(
    actor: ZLinkSessionActor,
    frameHeader: ZLinkStreamFrameHeader,
    payload: Message,
    signal?: AbortSignal
  ): Promise<boolean> {
    if (await this.relayRemoteActorPacket(actor, frameHeader, payload, signal)) {
      return true;
    }
    return await this.relayLocalActorPacket(actor, frameHeader, payload, signal);
  }

  private async relayLocalActorPacket(
    actor: ZLinkSessionActor,
    frameHeader: ZLinkStreamFrameHeader,
    payload: Message,
    signal?: AbortSignal
  ): Promise<boolean> {
    void signal;
    const state = this.actorManager?.getState(actor.actorId);
    const spotRid = state?.spotRid as RoutingId | undefined;
    const hasActiveSpot = spotRid !== undefined && this.spotManager?.hasActiveSpot(spotRid) === true;
    if (!hasActiveSpot) {
      return false;
    }
    const actorRef = state?.nativeActorRef as ActorRef | undefined;
    const localNodeRid = this.spotNodeRuntime?.primaryNode?.routingId as RoutingId | undefined;
    if (
      actorRef?.nodeRid !== undefined &&
      localNodeRid !== undefined &&
      !routingIdsEqual(actorRef.nodeRid as RoutingId, localNodeRid)
    ) {
      return false;
    }
    const responseTarget = this.streamBindingRuntime.captureBoundSessionResponseTarget(actor);
    const header = BindingMessage.from(Buffer.from(encodeStreamHeader(frameHeader)));
    try {
      if (frameHeader.kind === ZLinkStreamMessageKind.Request && frameHeader.requestSeq !== undefined) {
        try {
          const response = await this.requireSpotManager().dispatchRoutedActorPacket(
            spotRid,
            actor.actorId,
            [header, payload],
            true
          );
          const sent = this.sendCapturedOrCurrentBoundSessionResponse(
            responseTarget,
            actor.actorId,
            frameHeader.name,
            frameHeader.requestSeq,
            response,
            streamMetadataMap(frameHeader.metadata)
          );
          if (!sent) {
            throw new Error(`Actor '${actor.actorId}' local bound session response route is not ready.`);
          }
        } catch (error) {
          const sent = this.sendCapturedOrCurrentBoundSessionError(
            responseTarget,
            actor.actorId,
            frameHeader.name,
            frameHeader.requestSeq,
            error,
            streamMetadataMap(frameHeader.metadata)
          );
          if (!sent) {
            throw error;
          }
        }
        return true;
      }
      await this.requireSpotManager().dispatchRoutedActorPacket(
        spotRid,
        actor.actorId,
        [header, payload],
        false
      );
      return true;
    } finally {
      header.close();
    }
  }

  private async relayRemoteActorPacket(
    actor: ZLinkSessionActor,
    frameHeader: ZLinkStreamFrameHeader,
    payload: Message,
    signal?: AbortSignal
  ): Promise<boolean> {
    const responseTarget = this.streamBindingRuntime.captureBoundSessionResponseTarget(actor);
    const remoteTarget = this.actorManager?.getState(actor.actorId)?.remoteActorPacketTarget
      ?? this.sessionActorPacketTargets.get(actor)
      ?? this.sessionActorPacketTargetsByActor.get(sessionActorPacketTargetKey(actor))
      ?? this.sessionActorPacketTargetsByActorId.get(actor.actorId)
      ?? this.remoteActorPacketTargetForBoundActor(actor.ref);
    if (remoteTarget === undefined) {
      return false;
    }
    const localNodeRid = this.spotNodeRuntime?.primaryNode?.routingId as RoutingId | undefined;
    const request = {
      packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
      actorId: actor.actorId,
      routerChannelId: remoteTarget.routerChannelId,
      boundSessionTargetNodeRid: localNodeRid === undefined ? undefined : String(localNodeRid),
      boundSessionSpotRid: localNodeRid === undefined ? undefined : String(localNodeRid),
      header: Buffer.from(encodeStreamHeader(frameHeader)).toString('base64'),
      payload: Buffer.from(messageToBytes(payload)).toString('base64')
    };
    try {
      let reply: {
        readonly ok?: boolean;
        readonly error?: unknown;
        readonly response?: unknown;
        readonly deferredResponse?: boolean;
        readonly actorPacketTarget?: unknown;
      };
      const payload = BindingMessage.from(Buffer.from(JSON.stringify(request)));
      try {
        const parts = await this.routeTransport.requestRawToSpot({
          ...remoteTarget,
          spotKind: remoteTarget.spotKind ?? ZLinkSpotKind.User
        }, payload, {
          timeoutMs: this.options.registration.requestTimeoutMs,
          signal
        });
        try {
          if (parts.length === 0) {
            throw new Error(`Remote actor packet relay reply was empty for '${actor.actorId}'.`);
          }
          reply = JSON.parse(parts[0].getString('utf8')) as {
            readonly ok?: boolean;
            readonly error?: unknown;
            readonly response?: unknown;
            readonly deferredResponse?: boolean;
            readonly actorPacketTarget?: unknown;
          };
        } finally {
          parts.forEach((part) => part.close());
        }
      } finally {
        payload.close();
      }
      const actorPacketTarget = decodeRemoteActorPacketTarget(reply.actorPacketTarget);
      if (
        actorPacketTarget !== undefined &&
        (localNodeRid === undefined || !routingIdsEqual(actorPacketTarget.targetNodeRid, localNodeRid))
      ) {
        const state = this.actorManager?.getState(actor.actorId);
        if (typeof state?.setRemoteActorPacketTarget === 'function') {
          state.setRemoteActorPacketTarget(actorPacketTarget);
        }
        this.sessionActorPacketTargets.set(actor, actorPacketTarget);
        this.sessionActorPacketTargetsByActor.set(sessionActorPacketTargetKey(actor), actorPacketTarget);
        this.sessionActorPacketTargetsByActorId.set(actor.actorId, actorPacketTarget);
      } else if (reply.ok !== false) {
        const state = this.actorManager?.getState(actor.actorId);
        if (typeof state?.setRemoteActorPacketTarget === 'function') {
          state.setRemoteActorPacketTarget(undefined);
        }
        this.sessionActorPacketTargets.delete(actor);
        this.sessionActorPacketTargetsByActor.delete(sessionActorPacketTargetKey(actor));
        this.sessionActorPacketTargetsByActorId.delete(actor.actorId);
      }
      if (frameHeader.requestSeq !== undefined) {
          if (reply.deferredResponse === true && reply.ok !== false) {
            return true;
          }
          if (reply.ok === false) {
            const sent = this.sendCapturedOrCurrentBoundSessionError(
              responseTarget,
              actor.actorId,
              frameHeader.name,
              frameHeader.requestSeq,
              reply.error ?? 'Remote actor request failed.',
              streamMetadataMap(frameHeader.metadata)
            );
            if (!sent) {
              throw new Error(`Actor '${actor.actorId}' local bound session error response route is not ready.`);
            }
            return true;
          }
          const sent = this.sendCapturedOrCurrentBoundSessionResponse(
            responseTarget,
            actor.actorId,
            frameHeader.name,
            frameHeader.requestSeq,
            reply.response,
            streamMetadataMap(frameHeader.metadata)
          );
          if (!sent) {
            throw new Error(`Actor '${actor.actorId}' local bound session response route is not ready.`);
          }
      }
    } finally {
      void request;
    }
    return true;
  }

  private sendCapturedOrCurrentBoundSessionResponse(
    target: ZLinkBoundSessionResponseTarget | undefined,
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    response: unknown,
    metadata: ReadonlyMap<string, string>
  ): boolean {
    return target?.sendResponse(packetName, requestSeq, response, metadata)
      ?? this.streamBindingRuntime.sendLocalBoundSessionResponse(
        actorId,
        packetName,
        requestSeq,
        response,
        metadata
      );
  }

  private sendCapturedOrCurrentBoundSessionError(
    target: ZLinkBoundSessionResponseTarget | undefined,
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>
  ): boolean {
    return target?.sendError(packetName, requestSeq, error, metadata)
      ?? this.streamBindingRuntime.sendLocalBoundSessionError(
        actorId,
        packetName,
        requestSeq,
        error,
        metadata
      );
  }

  private actorPacketTargetForState(
    actorId: string,
    routerChannelIdHint?: string
  ): ZLinkRemoteActorPacketTarget | undefined {
    const state = this.actorManager?.getState(actorId);
    if (
      state?.remoteActorPacketTarget !== undefined &&
      (state.spotRid === undefined || String(state.remoteActorPacketTarget.spotRid) === String(state.spotRid))
    ) {
      return state.remoteActorPacketTarget;
    }
    const spotRid = state?.spotRid;
    if (spotRid !== undefined && state?.remoteActorPacketTarget !== undefined) {
      return {
        routerChannelId: state.remoteActorPacketTarget.routerChannelId,
        targetNodeRid: state.remoteActorPacketTarget.targetNodeRid,
        spotRid: normalizeRuntimeRoutingId(spotRid),
        spotKind: ZLinkSpotKind.User
      };
    }
    const actorRef = state?.nativeActorRef as ActorRef | undefined;
    const targetNodeRid = actorRef?.nodeRid as RoutingId | undefined
      ?? this.spotNodeRuntime?.primaryNode?.routingId as RoutingId | undefined;
    const routerChannelId = routerChannelIdHint
      ?? this.defaultSpotRouterChannelId()
      ?? this.defaultRouterChannelId();
    const localNodeRid = this.spotNodeRuntime?.primaryNode?.routingId as RoutingId | undefined;
    if (
      spotRid === undefined &&
      targetNodeRid !== undefined &&
      localNodeRid !== undefined &&
      routingIdsEqual(targetNodeRid, localNodeRid)
    ) {
      return undefined;
    }
    if (spotRid === undefined || targetNodeRid === undefined || routerChannelId === undefined) {
      return undefined;
    }
    return {
      routerChannelId,
      targetNodeRid: normalizeRuntimeRoutingId(targetNodeRid),
      spotRid: normalizeRuntimeRoutingId(spotRid),
      spotKind: ZLinkSpotKind.User
    };
  }

  private remoteActorPacketTargetForBoundActor(actorRef: ActorRef): ZLinkRemoteActorPacketTarget | undefined {
    return this.remoteActorPacketTargetForActorRef(actorRef);
  }

  private remoteActorPacketTargetForActorRef(actorRef: ActorRef): ZLinkRemoteActorPacketTarget | undefined {
    const targetNodeRid = actorRef.nodeRid as RoutingId;
    const localNodeRid = this.spotNodeRuntime?.primaryNode?.routingId as RoutingId | undefined;
    if (localNodeRid !== undefined && routingIdsEqual(localNodeRid, targetNodeRid)) {
      return undefined;
    }
    const routerChannelId = this.defaultSpotRouterChannelId()
      ?? this.defaultRouterChannelId();
    if (routerChannelId === undefined) {
      return undefined;
    }
    return {
      routerChannelId,
      targetNodeRid,
      spotRid: targetNodeRid,
      spotKind: ZLinkSpotKind.Entry
    };
  }

  private defaultRouterChannelId(): string | undefined {
    const candidates = [
      ...this.options.registration.routeChannels,
      ...[...this.options.registration.spotNodes.entries()]
        .filter(([, spotNode]) => spotNode.router !== undefined)
        .map(([spotNodeName]) => spotNodeName)
    ];
    return candidates.length === 1 ? candidates[0] : undefined;
  }

  private defaultSpotRouterChannelId(): string | undefined {
    const candidates = [...this.options.registration.spotNodes.entries()]
      .filter(([, spotNode]) => spotNode.router !== undefined)
      .map(([spotNodeName]) => spotNodeName);
    return candidates.length === 1 ? candidates[0] : undefined;
  }

  private primarySpotMeshName(): string | undefined {
    const names = [...this.options.registration.spotNodes.keys()];
    return names.length === 1 ? names[0] : undefined;
  }

  private spotLocationMeshNames(): readonly string[] {
    return [...new Set([
      ...this.options.registration.spotNodes.keys(),
      ...this.options.registration.routeChannels.keys()
    ])];
  }

  private spotRouterChannelIdByMesh(): (meshName: string) => string {
    const routeChannels = [...this.options.registration.routeChannelOptions.values()];
    const mapped = new Map<string, string>();
    for (const [spotMeshName, spotNode] of this.options.registration.spotNodes.entries()) {
      const conventionalRouteChannelId = `${spotMeshName}.route`;
      if (this.options.registration.routeChannelOptions.has(conventionalRouteChannelId)) {
        mapped.set(spotMeshName, conventionalRouteChannelId);
        continue;
      }
      const spotNodeRid = spotNode.router?.routingId ?? spotNode.routingId;
      if (spotNodeRid === undefined) {
        continue;
      }
      const candidates = routeChannels
        .filter((routeChannel) =>
          routeChannel.routingId !== undefined &&
          routingIdsEqual(routeChannel.routingId, spotNodeRid)
        )
        .map((routeChannel) => routeChannel.routerChannelId);
      if (candidates.length === 1) {
        mapped.set(spotMeshName, candidates[0]);
      }
    }
    return (meshName) => mapped.get(meshName) ?? meshName;
  }

  private canUseRouterChannel(routerChannelId: string): boolean {
    return this.options.registration.routeChannels.has(routerChannelId)
      || this.options.registration.spotNodes.get(routerChannelId)?.router !== undefined;
  }
}

function sessionActorPacketTargetKey(actor: ZLinkSessionActor): string {
  return `${String(actor.ref.nodeRid)}:${actor.actorId}:${String(actor.ref.generation)}`;
}

function decodeRemoteActorPacketRelayPayload(payload: unknown): {
  readonly actorId: string;
  readonly routerChannelId?: string;
  readonly boundSessionTargetNodeRid?: string;
  readonly boundSessionSpotRid?: string;
  readonly header: string;
  readonly payload: string;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string' ||
    typeof (payload as { header?: unknown }).header !== 'string' ||
    typeof (payload as { payload?: unknown }).payload !== 'string'
  ) {
    throw new Error('Remote actor packet relay payload is invalid.');
  }
  return {
    actorId: (payload as { actorId: string }).actorId,
    routerChannelId: typeof (payload as { routerChannelId?: unknown }).routerChannelId === 'string'
      ? (payload as { routerChannelId: string }).routerChannelId
      : undefined,
    boundSessionTargetNodeRid: typeof (payload as { boundSessionTargetNodeRid?: unknown }).boundSessionTargetNodeRid === 'string'
      ? (payload as { boundSessionTargetNodeRid: string }).boundSessionTargetNodeRid
      : undefined,
    boundSessionSpotRid: typeof (payload as { boundSessionSpotRid?: unknown }).boundSessionSpotRid === 'string'
      ? (payload as { boundSessionSpotRid: string }).boundSessionSpotRid
      : undefined,
    header: (payload as { header: string }).header,
    payload: (payload as { payload: string }).payload
  };
}

function decodeRemoteActorJoinPayload(payload: unknown): {
  readonly spotRid: string;
  readonly actorId: string;
  readonly actorType: string;
  readonly actorNodeRid: string;
  readonly actorNodeRidHex?: string;
  readonly actorGeneration: string;
  readonly sourceSpotRid?: string;
  readonly routerChannelId?: string;
  readonly boundSessionRouterChannelId?: string;
  readonly boundSessionTargetNodeRid?: string;
  readonly boundSessionSpotRid?: string;
  readonly request: string;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== ZLINK_REMOTE_ACTOR_JOIN_PACKET ||
    typeof (payload as { spotRid?: unknown }).spotRid !== 'string' ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string' ||
    typeof (payload as { actorType?: unknown }).actorType !== 'string' ||
    typeof (payload as { actorNodeRid?: unknown }).actorNodeRid !== 'string' ||
    typeof (payload as { actorGeneration?: unknown }).actorGeneration !== 'string' ||
    typeof (payload as { request?: unknown }).request !== 'string'
  ) {
    throw new Error('Remote actor join payload is invalid.');
  }
  return {
    spotRid: (payload as { spotRid: string }).spotRid,
    actorId: (payload as { actorId: string }).actorId,
    actorType: (payload as { actorType: string }).actorType,
    actorNodeRid: (payload as { actorNodeRid: string }).actorNodeRid,
    actorNodeRidHex: typeof (payload as { actorNodeRidHex?: unknown }).actorNodeRidHex === 'string'
      ? (payload as { actorNodeRidHex: string }).actorNodeRidHex
      : undefined,
    actorGeneration: (payload as { actorGeneration: string }).actorGeneration,
    sourceSpotRid: typeof (payload as { sourceSpotRid?: unknown }).sourceSpotRid === 'string'
      ? (payload as { sourceSpotRid: string }).sourceSpotRid
      : undefined,
    routerChannelId: typeof (payload as { routerChannelId?: unknown }).routerChannelId === 'string'
      ? (payload as { routerChannelId: string }).routerChannelId
      : undefined,
    boundSessionRouterChannelId: typeof (payload as { boundSessionRouterChannelId?: unknown }).boundSessionRouterChannelId === 'string'
      ? (payload as { boundSessionRouterChannelId: string }).boundSessionRouterChannelId
      : undefined,
    boundSessionTargetNodeRid: typeof (payload as { boundSessionTargetNodeRid?: unknown }).boundSessionTargetNodeRid === 'string'
      ? (payload as { boundSessionTargetNodeRid: string }).boundSessionTargetNodeRid
      : undefined,
    boundSessionSpotRid: typeof (payload as { boundSessionSpotRid?: unknown }).boundSessionSpotRid === 'string'
      ? (payload as { boundSessionSpotRid: string }).boundSessionSpotRid
      : undefined,
    request: (payload as { request: string }).request
  };
}

function decodeRemoteBoundSessionSendPayload(payload: unknown): {
  readonly actorId: string;
  readonly message: unknown;
  readonly boundPacketName?: string;
  readonly metadata?: Record<string, string>;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string'
  ) {
    throw new Error('Remote bound session send payload is invalid.');
  }
  const rawMetadata = (payload as { metadata?: unknown }).metadata;
  const metadata: Record<string, string> = {};
  if (typeof rawMetadata === 'object' && rawMetadata !== null) {
    for (const [key, value] of Object.entries(rawMetadata)) {
      if (typeof value === 'string') {
        metadata[key] = value;
      }
    }
  }
  return {
    actorId: (payload as { actorId: string }).actorId,
    message: (payload as { message?: unknown }).message,
    boundPacketName: typeof (payload as { boundPacketName?: unknown }).boundPacketName === 'string'
      ? (payload as { boundPacketName: string }).boundPacketName
      : undefined,
    metadata
  };
}

function decodeRemoteBoundSessionResponsePayload(payload: unknown): {
  readonly actorId: string;
  readonly message: unknown;
  readonly boundPacketName: string;
  readonly requestSeq: bigint;
  readonly metadata?: Record<string, string>;
  readonly actorPacketTarget?: unknown;
} {
  const decoded = decodeRemoteBoundSessionControlPayload(payload, ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET);
  return {
    actorId: decoded.actorId,
    message: (payload as { message?: unknown }).message,
    boundPacketName: decoded.boundPacketName,
    requestSeq: decoded.requestSeq,
    metadata: decoded.metadata,
    actorPacketTarget: decoded.actorPacketTarget
  };
}

function decodeRemoteBoundSessionErrorPayload(payload: unknown): {
  readonly actorId: string;
  readonly error: unknown;
  readonly boundPacketName: string;
  readonly requestSeq: bigint;
  readonly metadata?: Record<string, string>;
  readonly actorPacketTarget?: unknown;
} {
  const decoded = decodeRemoteBoundSessionControlPayload(payload, ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET);
  return {
    actorId: decoded.actorId,
    error: (payload as { error?: unknown }).error,
    boundPacketName: decoded.boundPacketName,
    requestSeq: decoded.requestSeq,
    metadata: decoded.metadata,
    actorPacketTarget: decoded.actorPacketTarget
  };
}

function decodeRemoteBoundSessionControlPayload(payload: unknown, packetName: string): {
  readonly actorId: string;
  readonly boundPacketName: string;
  readonly requestSeq: bigint;
  readonly metadata?: Record<string, string>;
  readonly actorPacketTarget?: unknown;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== packetName ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string' ||
    typeof (payload as { boundPacketName?: unknown }).boundPacketName !== 'string' ||
    typeof (payload as { requestSeq?: unknown }).requestSeq !== 'string'
  ) {
    throw new Error('Remote bound session control payload is invalid.');
  }
  const rawMetadata = (payload as { metadata?: unknown }).metadata;
  const metadata: Record<string, string> = {};
  if (typeof rawMetadata === 'object' && rawMetadata !== null) {
    for (const [key, value] of Object.entries(rawMetadata)) {
      if (typeof value === 'string') {
        metadata[key] = value;
      }
    }
  }
  return {
    actorId: (payload as { actorId: string }).actorId,
    boundPacketName: (payload as { boundPacketName: string }).boundPacketName,
    requestSeq: BigInt((payload as { requestSeq: string }).requestSeq),
    metadata,
    actorPacketTarget: (payload as { actorPacketTarget?: unknown }).actorPacketTarget
  };
}

function streamMetadataMap(metadata: unknown): ReadonlyMap<string, string> {
  if (metadata instanceof Map) {
    return new Map(metadata);
  }
  const maybeValues = metadata as { values?: unknown } | undefined;
  if (maybeValues?.values instanceof Map) {
    return new Map(maybeValues.values);
  }
  return new Map();
}

class ZLinkNativeFallbackBoundSession implements ZLinkBoundSession {
  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    private readonly routedTransport: ZLinkActorRoutedJoinTransport,
    private readonly nodeProvider: () => ZLinkBackendSpotNode,
    private readonly actorRefProvider: () => ActorRef | undefined,
    private readonly remoteBoundSessionTargetProvider: () => ZLinkRemoteBoundSessionTarget | undefined,
    private readonly remoteActorPacketTargetProvider: () => ZLinkRemoteActorPacketTarget | undefined,
    private readonly requestTimeoutMs: number | undefined,
    private readonly actorId: string
  ) {}

  send(message: unknown): ZLinkBoundSessionSendCall {
    return new ZLinkNativeFallbackBoundSessionSendCall(
      this.runtime,
      this.routedTransport,
      this.nodeProvider,
      this.actorRefProvider,
      this.remoteBoundSessionTargetProvider,
      this.requestTimeoutMs,
      this.actorId,
      message
    );
  }

  async disconnect(signal?: AbortSignal): Promise<void> {
    const remoteTarget = this.remoteBoundSessionTargetProvider() ?? this.remoteActorPacketTargetProvider();
    if (remoteTarget !== undefined) {
      const spotKind: ZLinkSpotKind | undefined =
        'spotKind' in remoteTarget ? remoteTarget.spotKind as ZLinkSpotKind | undefined : undefined;
      const header: ZLinkStreamFrameHeader = {
        kind: ZLinkStreamMessageKind.Send,
        codec: ZLinkStreamCodec.Raw,
        flags: ZLinkStreamHeaderFlags.None,
        name: ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET,
        metadata: new Map()
      };
      const payload = {
        packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
        actorId: this.actorId,
        routerChannelId: remoteTarget.routerChannelId,
        header: Buffer.from(encodeStreamHeader(header)).toString('base64'),
        payload: Buffer.alloc(0).toString('base64')
      };
      if (this.routedTransport.requestRawToSpot !== undefined) {
        const rawPayload = BindingMessage.from(Buffer.from(JSON.stringify(payload)));
        try {
          const replyParts = await this.routedTransport.requestRawToSpot(
            {
              routerChannelId: remoteTarget.routerChannelId,
              targetNodeRid: remoteTarget.targetNodeRid,
              spotRid: remoteTarget.spotRid,
              spotKind: spotKind ?? ZLinkSpotKind.Entry
            },
            rawPayload,
            {
              timeoutMs: this.requestTimeoutMs,
              signal
            }
          );
          for (const part of replyParts) {
            part.close();
          }
        } finally {
          rawPayload.close();
        }
        return;
      }
      await this.routedTransport.sendToSpot(
        {
          routerChannelId: remoteTarget.routerChannelId,
          targetNodeRid: remoteTarget.targetNodeRid,
          spotRid: remoteTarget.spotRid,
          spotKind: spotKind ?? ZLinkSpotKind.Entry
        },
        payload,
        { packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET, signal }
      );
      return;
    }
    const actorRef = this.actorRefProvider();
    if (actorRef !== undefined) {
      await nextNativeGatewayTurn();
      await this.runtime.disconnectNativeBoundSession(this.nodeProvider(), actorRef, signal);
      return;
    }
    await this.runtime.disconnectBoundSession(this.actorId, signal);
  }
}

class ZLinkNativeFallbackBoundSessionSendCall implements ZLinkBoundSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();
  private executed = false;

  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    private readonly routedTransport: ZLinkActorRoutedJoinTransport,
    private readonly nodeProvider: () => ZLinkBackendSpotNode,
    private readonly actorRefProvider: () => ActorRef | undefined,
    private readonly remoteBoundSessionTargetProvider: () => ZLinkRemoteBoundSessionTarget | undefined,
    private readonly requestTimeoutMs: number | undefined,
    private readonly actorId: string,
    private readonly message: unknown
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  packetName(packetName: string): this {
    this.selectedPacketName = packetName;
    return this;
  }

  submit(signal?: AbortSignal): void {
    void this.submitAsync(signal).catch(() => undefined);
  }

  private async submitAsync(signal?: AbortSignal): Promise<void> {
    if (this.executed) {
      throw new Error('Bound session send already submitted.');
    }
    this.executed = true;
    const remoteTarget = this.remoteBoundSessionTargetProvider();
    if (remoteTarget !== undefined) {
      const payload = {
        packetName: ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET,
        actorId: this.actorId,
        message: this.message,
        boundPacketName: this.selectedPacketName,
        metadata: Object.fromEntries(this.selectedMetadata)
      };
      if (this.routedTransport.requestRawToSpot !== undefined) {
        const rawPayload = BindingMessage.from(Buffer.from(JSON.stringify(payload)));
        try {
          const replyParts = await this.routedTransport.requestRawToSpot(
            {
              routerChannelId: remoteTarget.routerChannelId,
              targetNodeRid: remoteTarget.targetNodeRid,
              spotRid: remoteTarget.spotRid,
              spotKind: ZLinkSpotKind.Entry
            },
            rawPayload,
            {
              timeoutMs: this.requestTimeoutMs,
              signal
            }
          );
          for (const part of replyParts) {
            part.close();
          }
        } finally {
          rawPayload.close();
        }
        return;
      }
      await this.routedTransport.sendToSpot(
        {
          routerChannelId: remoteTarget.routerChannelId,
          targetNodeRid: remoteTarget.targetNodeRid,
          spotRid: remoteTarget.spotRid,
          spotKind: ZLinkSpotKind.Entry
        },
        payload,
        { packetName: ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET, signal }
      );
      return;
    }
    if (this.runtime.sendLocalBoundSession(
      this.actorId,
      this.message,
      this.selectedPacketName,
      this.selectedMetadata
    )) {
      return;
    }
    const actorRef = this.actorRefProvider();
    if (actorRef !== undefined) {
      await nextNativeGatewayTurn();
      await this.runtime.sendNativeBoundSession(
        this.nodeProvider(),
        actorRef,
        this.message,
        this.selectedPacketName,
        this.selectedMetadata,
        signal
      );
      return;
    }
    await this.runtime.sendBoundSession(
      this.actorId,
      this.message,
      this.selectedPacketName,
      this.selectedMetadata,
      signal
    );
  }
}

function nextNativeGatewayTurn(): Promise<void> {
  return new Promise((resolve) => setImmediate(resolve));
}

interface ZLinkLocalFirstActorJoinCoordinatorOptions {
  readonly localSpotManager: () => DefaultZLinkSpotManager | undefined;
  readonly nativeNode: () => ZLinkBackendSpotNode;
  readonly native: ZLinkActorJoinCoordinator;
  readonly actorBinder?: (actorRef: ActorRef, signal?: AbortSignal, force?: boolean) => Promise<void>;
}

class ZLinkLocalFirstActorJoinCoordinator implements ZLinkActorJoinCoordinator {
  constructor(private readonly options: ZLinkLocalFirstActorJoinCoordinatorOptions) {}

  async joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    const localSpotManager = this.options.localSpotManager();
    if (
      localSpotManager === undefined ||
      !localSpotManager.hasActiveSpot(spotRid)
    ) {
      return this.options.native.joinSpot(actor, state, spotRid, request, timeoutMs, signal);
    }
    const result = await localSpotManager.admitActorJoin(
      spotRid,
      actor,
      request,
      (spot: ZLinkSpot) => state.setJoinedSpot(spotRid, spot),
      signal
    );
    const nativeActorRef = state.nativeActorRef;
    const actorRef = nativeActorRef === undefined
      ? localActorRef(nodeRidForLocalActor(this.options.nativeNode), actor.actorId)
      : {
          nodeRid: nativeActorRef.nodeRid as unknown as RoutingId,
          actorId: nativeActorRef.actorId,
          generation: nativeActorRef.generation
        } as ActorRef;
    if (result.accepted) {
      await this.options.actorBinder?.(actorRef, signal, true);
    }
    return {
      accepted: result.accepted,
      actor: result.accepted ? actorRef : undefined,
      reply: result.reply as Message | undefined
    };
  }

  async joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    // Entry Spot join always goes through the native round-trip. The target
    // node's runtime drains the admission request from `recvActorJoin`, runs
    // `onActorJoin`, and replies; core commits membership and routes the reply
    // back here. This is identical for a local or remote target node, so the
    // admission behaviour does not diverge by caller locality.
    return this.options.native.joinEntrySpot(actor, state, nodeRid, request, timeoutMs, signal);
  }
}

function nodeRidForLocalActor(nodeProvider: () => ZLinkBackendSpotNode): RoutingId {
  return nodeProvider().routingId;
}

function localActorRef(nodeRid: RoutingId, actorId: string): ActorRef {
  return { nodeRid, actorId, generation: 0n } as ActorRef;
}

function routingIdsEqual(left: RoutingId, right: RoutingId): boolean {
  const leftHex = (left as { toHex?: () => string }).toHex?.();
  const rightHex = (right as { toHex?: () => string }).toHex?.();
  if (leftHex !== undefined && rightHex !== undefined) {
    return leftHex === rightHex;
  }
  return String(left) === String(right);
}

class ZLinkLazyNativeJoinCoordinator implements ZLinkActorJoinCoordinator {
  constructor(
    private readonly nodeProvider: () => ZLinkBackendSpotNode,
    private readonly remoteAddressResolver?: ZLinkSpotRemoteAddressResolver,
    private readonly routedTransport?: ZLinkActorRoutedJoinTransport,
    private readonly remoteActorBinder?: (actorRef: ActorRef, signal?: AbortSignal, force?: boolean) => Promise<void>,
    private readonly locationLifecycleProvider?: () => ZLinkLocationLifecycle | undefined
  ) {}

  joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    return new ZLinkActorNativeJoinCoordinator({
      node: this.nodeProvider(),
      remoteAddressResolver: this.remoteAddressResolver,
      routedTransport: this.routedTransport,
      locationLifecycle: this.locationLifecycleProvider?.(),
      remoteActorBinder: this.remoteActorBinder
    })
      .joinSpot(actor, state, spotRid, request, timeoutMs, signal);
  }

  joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    return new ZLinkActorNativeJoinCoordinator({
      node: this.nodeProvider(),
      remoteAddressResolver: this.remoteAddressResolver,
      routedTransport: this.routedTransport,
      locationLifecycle: this.locationLifecycleProvider?.(),
      remoteActorBinder: this.remoteActorBinder
    })
      .joinEntrySpot(actor, state, nodeRid, request, timeoutMs, signal);
  }
}

interface ZLinkMonitoringRuntimeOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly channelRuntime: ZLinkChannelRuntimeManager;
  readonly spotNodes: ReadonlyMap<string, ZLinkBackendSpotNode>;
  readonly locationRuntime?: ZLinkLocationRuntime;
  readonly monitoringAdapter: ZLinkMonitoringBackendAdapter;
  readonly publisher: ZLinkRuntimeEventPublisher;
}

class ZLinkMonitoringRuntime {
  private readonly monitors: ZLinkBackendSocketMonitor[] = [];

  constructor(private readonly options: ZLinkMonitoringRuntimeOptions) {}

  start(state: ZLinkFrameworkRuntimeState): void {
    const monitoring = this.options.registration.monitoring;
    if (monitoring === undefined) {
      return;
    }
    for (const registration of monitoring.socket ?? []) {
      const monitor = this.options.channelRuntime.openMonitoringSource(registration.sourceName, this.options.monitoringAdapter);
      this.monitors.push(monitor);
      new ZLinkSocketMonitoringSource(registration, monitor, this.options.publisher).start();
    }
    for (const registration of monitoring.locationRuntime ?? []) {
      const query = this.options.locationRuntime;
      if (query === undefined) {
        throw new Error(`Monitoring location runtime source '${registration.sourceName}' requires location stores.`);
      }
      const source = new ZLinkLocationRuntimeMonitoringSource(registration, query, this.options.publisher);
      state.listenerTasks.push(state.taskRunner.run(
        `monitoring:location-runtime:${registration.sourceName}`,
        (signal) => runPollingMonitoringSource(registration.intervalMs, signal, () => source.pollOnce(signal))
      ));
    }
    for (const registration of monitoring.spot ?? []) {
      const spotNode = this.options.spotNodes.get(registration.sourceName);
      if (spotNode === undefined) {
        throw new Error(`Monitoring spot source '${registration.sourceName}' is not registered.`);
      }
      const source = new ZLinkSpotMonitoringSource(registration, spotNode, this.options.publisher);
      state.listenerTasks.push(state.taskRunner.run(
        `monitoring:spot:${registration.sourceName}`,
        (signal) => runPollingMonitoringSource(registration.intervalMs, signal, () => source.pollOnce())
      ));
    }
  }

  async dispose(): Promise<void> {
    const monitors = [...this.monitors];
    this.monitors.length = 0;
    await Promise.allSettled(monitors.reverse().map((monitor) => monitor.dispose()));
  }
}

async function runPollingMonitoringSource(
  intervalMs: number,
  signal: AbortSignal,
  pollOnce: () => Promise<void>
): Promise<void> {
  while (!signal.aborted) {
    await pollOnce();
    await delayMonitoringPoll(intervalMs, signal);
  }
}

function delayMonitoringPoll(intervalMs: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve) => {
    const timeout = setTimeout(resolve, intervalMs);
    signal.addEventListener('abort', () => {
      clearTimeout(timeout);
      resolve();
    }, { once: true });
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

function formatErrorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function isStreamPayloadCodec(codec: unknown): codec is ZLinkStreamPayloadCodec {
  return typeof codec === 'object'
    && codec !== null
    && typeof (codec as { encode?: unknown }).encode === 'function';
}
