import { ZLinkNodeBackendAdapterFactory } from '../backend';
import type { ZLinkBackendAdapterFactory, ZLinkBackendContext, ZLinkBackendSpotNode } from '../backend';
import type { ZLinkFrameworkRegistration } from '../configuration';
import type {
  ActorRef,
  Message,
  RoutingId,
  ZLinkActor,
  ZLinkActorJoinResult,
  ZLinkProviderResolver,
  ZLinkSpot
} from '../../contracts';
import {
  ZLinkChannelRuntimeManager,
  ZLinkRuntimeChannelTransport,
  ZLinkRuntimeRouteTransport
} from '../channels';
import { ZLinkFrameworkRuntimeState } from '../execution';
import { DefaultZLinkSpotManager, ZLinkRuntimeSpotPublisherTransport, ZLinkSpotNodeRuntimeManager } from '../spots';
import type {
  DefaultZLinkActorManager,
  ZLinkActorJoinCoordinator,
  ZLinkActorManagerOptions,
  ZLinkActorRuntimeState
} from '../actors';
import { ZLinkActorNativeJoinCoordinator } from '../actors';
import {
  DefaultZLinkBoundSessionFactory,
  ZLinkStreamBindingRuntime,
  ZLinkStreamRuntimeManager
} from '../streams';

export interface ZLinkFrameworkRuntime {
  readonly isStarted: boolean;
  start(): Promise<void>;
  stop(): Promise<void>;
}

export interface ZLinkFrameworkRuntimeHostOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly lifecycleSink?: string[];
  readonly providerResolver?: ZLinkProviderResolver;
}

export class ZLinkFrameworkRuntimeHost implements ZLinkFrameworkRuntime {
  private readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  private readonly lifecycleSink?: string[];
  private state?: ZLinkFrameworkRuntimeState;
  private channelRuntime?: ZLinkChannelRuntimeManager;
  private spotNodeRuntime?: ZLinkSpotNodeRuntimeManager;
  private streamRuntime?: ZLinkStreamRuntimeManager;
  private actorManager?: DefaultZLinkActorManager;
  private spotManager?: DefaultZLinkSpotManager;
  readonly channelTransport = new ZLinkRuntimeChannelTransport(() => this.channelRuntime);
  readonly routeTransport = new ZLinkRuntimeRouteTransport(() => this.channelRuntime);
  readonly spotPublisherTransport = new ZLinkRuntimeSpotPublisherTransport(() => this.spotNodeRuntime);
  readonly streamBindingRuntime = new ZLinkStreamBindingRuntime();
  readonly boundSessionFactory = new DefaultZLinkBoundSessionFactory(this.streamBindingRuntime);

  constructor(readonly options: ZLinkFrameworkRuntimeHostOptions, internalOptions?: unknown) {
    this.backendAdapterFactory = resolveBackendAdapterFactory(internalOptions);
    this.lifecycleSink = options.lifecycleSink;
  }

  get isStarted(): boolean {
    return this.state !== undefined;
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
    try {
      this.state = new ZLinkFrameworkRuntimeState(context);
      channelRuntime = new ZLinkChannelRuntimeManager(this.options.registration, channelAdapter, context);
      this.state.listenerTasks.push(...channelRuntime.start(this.state.taskRunner));
      this.channelRuntime = channelRuntime;
      spotNodeRuntime = new ZLinkSpotNodeRuntimeManager({
        registration: this.options.registration,
        backendAdapterFactory: this.backendAdapterFactory,
        context,
        providerResolver: this.options.providerResolver,
        actorDestroyer: (node, entryNodeRid, actor, signal) => {
          if (this.actorManager === undefined) {
            throw new Error('Entry Spot actor destroy requires ZLINK_ACTOR_MANAGER.');
          }
          return this.actorManager.destroyActor(node, entryNodeRid, actor, signal);
        }
      });
      await spotNodeRuntime.start();
      this.spotNodeRuntime = spotNodeRuntime;
      streamRuntime = new ZLinkStreamRuntimeManager({
        registration: this.options.registration,
        backendAdapterFactory: this.backendAdapterFactory,
        context,
        bindingRuntime: this.streamBindingRuntime,
        spotNodes: spotNodeRuntime.nodesByName,
        providerResolver: this.options.providerResolver
      });
      streamRuntime.start();
      this.streamRuntime = streamRuntime;
      this.lifecycleSink?.push('framework:started');
    } catch (error) {
      await streamRuntime?.dispose();
      await spotNodeRuntime?.dispose();
      await channelRuntime?.dispose();
      await context.dispose();
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
    this.state = undefined;
    this.channelRuntime = undefined;
    this.spotNodeRuntime = undefined;
    this.streamRuntime = undefined;
    this.lifecycleSink?.push('framework:stop');
    await streamRuntime?.dispose();
    await spotNodeRuntime?.dispose();
    await channelRuntime?.dispose();
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

  createActorManagerOptions(): Pick<
    ZLinkActorManagerOptions,
    | 'joinCoordinator'
    | 'nativeActorNode'
    | 'actorCreatedNodeRidProvider'
    | 'actorCreatedNotifier'
    | 'actorDestroyedCleanup'
  > {
    return {
      joinCoordinator: new ZLinkLocalFirstActorJoinCoordinator({
        localSpotManager: () => this.spotManager,
        nativeNode: () => this.requirePrimarySpotNode(),
        entryCallbacks: {
          onJoinActor: (actor, signal) =>
            this.spotNodeRuntime?.notifyPrimaryEntrySpotActorJoined(actor, signal) ?? Promise.resolve(),
          onLeaveActor: (actor, signal) =>
            this.spotNodeRuntime?.notifyPrimaryEntrySpotActorLeft(actor, signal) ?? Promise.resolve()
        },
        native: new ZLinkLazyNativeJoinCoordinator(() => this.requirePrimarySpotNode())
      }),
      actorCreatedNodeRidProvider: () => this.spotNodeRuntime?.primaryNode?.routingId,
      actorCreatedNotifier: (nodeRid, actor, signal) =>
        this.spotNodeRuntime?.notifyEntrySpotActorCreated(nodeRid, actor, signal) ?? Promise.resolve(),
      actorDestroyedCleanup: (actorId) => this.streamBindingRuntime.unbindActor(actorId)
    };
  }

  createSpotManagerOptions(): object {
    return {
      nodeRid: undefined,
      nodeRidProvider: () => this.spotNodeRuntime?.primaryNode?.routingId,
      entryNodeRid: undefined,
      entryNodeRidProvider: () => this.spotNodeRuntime?.primaryNode?.routingId,
      entrySpotCallbacks: {
        onJoinActor: (actor: ZLinkActor, signal?: AbortSignal) =>
          this.spotNodeRuntime?.notifyPrimaryEntrySpotActorJoined(actor, signal) ?? Promise.resolve(),
        onLeaveActor: (actor: ZLinkActor, signal?: AbortSignal) =>
          this.spotNodeRuntime?.notifyPrimaryEntrySpotActorLeft(actor, signal) ?? Promise.resolve()
      }
    };
  }

  requirePrimarySpotNode(): ZLinkBackendSpotNode {
    const node = this.spotNodeRuntime?.primaryNode;
    if (node === undefined) {
      throw new Error('Primary Entry Spot node is not started.');
    }
    return node;
  }
}

interface ZLinkLocalFirstActorJoinCoordinatorOptions {
  readonly localSpotManager: () => DefaultZLinkSpotManager | undefined;
  readonly nativeNode: () => ZLinkBackendSpotNode;
  readonly entryCallbacks: {
    onJoinActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
    onLeaveActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
  };
  readonly native: ZLinkActorJoinCoordinator;
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
  ): Promise<ZLinkActorJoinResult> {
    const localSpotManager = this.options.localSpotManager();
    if (localSpotManager === undefined || !localSpotManager.hasActiveSpot(spotRid)) {
      return this.options.native.joinSpot(actor, state, spotRid, request, timeoutMs, signal);
    }

    const result = await localSpotManager.admitActorJoin(
      spotRid,
      actor,
      request,
      (spot: ZLinkSpot) => state.setJoinedSpot(spotRid, spot),
      signal
    );
    const actorRef = state.nativeActorRef;
    return {
      resultCode: result.accepted ? 0 : 1,
      actor: actorRef === undefined
        ? localActorRef(nodeRidForLocalActor(this.options.nativeNode), actor.actorId)
        : {
            nodeRid: actorRef.nodeRid as unknown as RoutingId,
            actorId: actorRef.actorId,
            generation: actorRef.generation
          } as ActorRef,
      reply: result.reply
    };
  }

  async joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ActorRef> {
    void timeoutMs;
    const localSpotManager = this.options.localSpotManager();
    if (
      localSpotManager === undefined ||
      state.spotRid === undefined ||
      !localSpotManager.hasActiveSpot(state.spotRid)
    ) {
      return this.options.native.joinEntrySpot(actor, state, nodeRid, timeoutMs, signal);
    }

    state.clearJoinedSpot();
    await this.options.entryCallbacks.onJoinActor(actor, signal);
    const actorRef = state.nativeActorRef;
    return actorRef === undefined
      ? localActorRef(nodeRid, actor.actorId)
      : {
          nodeRid: actorRef.nodeRid as unknown as RoutingId,
          actorId: actorRef.actorId,
          generation: actorRef.generation
        } as ActorRef;
  }
}

function nodeRidForLocalActor(nodeProvider: () => ZLinkBackendSpotNode): RoutingId {
  return nodeProvider().routingId;
}

function localActorRef(nodeRid: RoutingId, actorId: string): ActorRef {
  return { nodeRid, actorId, generation: 0n } as ActorRef;
}

class ZLinkLazyNativeJoinCoordinator implements ZLinkActorJoinCoordinator {
  constructor(private readonly nodeProvider: () => ZLinkBackendSpotNode) {}

  joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult> {
    return new ZLinkActorNativeJoinCoordinator({ node: this.nodeProvider() })
      .joinSpot(actor, state, spotRid, request, timeoutMs, signal);
  }

  joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ActorRef> {
    return new ZLinkActorNativeJoinCoordinator({ node: this.nodeProvider() })
      .joinEntrySpot(actor, state, nodeRid, timeoutMs, signal);
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
