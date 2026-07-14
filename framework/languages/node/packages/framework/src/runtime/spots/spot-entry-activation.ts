import type {
  ActorRef,
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkChannelClient,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkFanoutClient,
  ZLinkMessage,
  ZLinkMessageSerializer,
  ZLinkProviderResolver,
  ZLinkRuntimeEventPublisher,
  ZLinkSpot,
  ZLinkSpotPublisherClient
} from '../../contracts';
import type {
  ZLinkEntrySpotActorRequestHandlerRegistration,
  ZLinkEntrySpotActorSendHandlerRegistration,
  ZLinkEntrySpotPacketHandlerRegistration,
  ZLinkEntrySpotSubscriptionHandlerRegistration,
  ZLinkEntrySpotTimerHandlerRegistration
} from '../../contracts/Configuration/RegistrationTypes';
import type { Message } from '../../contracts/Common/Message';
import { throwIfAborted } from '../abort';
import { routingIdsEqual } from '../routing-id';
import type { ZLinkRemoteBoundSessionTarget } from '../actors';
import {
  ZLinkActorDispatchMailboxSet,
  ZLinkSpotActorHandlerRegistryRuntime
} from '../actors';
import type {
  ZLinkBackendActorRecvInfo,
  ZLinkBackendSpot,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import type { ZLinkDispatchErrorReporter } from '../channels';
import { ZLinkSpotWorkerRuntime } from '../workers';
import { ZLinkSpotActorPacketDispatch } from './spot-actor-packet-dispatch';
import {
  ZLinkSpotActorJoinDispatch,
  type ZLinkDetachedTaskRunner
} from './spot-actor-join-dispatch';
import {
  applyEntrySpotHandlerRegistrations,
  DefaultZLinkSpotHandlerRegistry
} from './spot-handler-registry';
import {
  DefaultZLinkSpotOutbound,
  type ZLinkSpotRoutedTransport
} from './spot-outbound';
import { createProviderInstance } from './spot-provider';
import { ZLinkSpotSerialExecutor } from './spot-serial-executor';
import {
  addEntrySpotTimerRegistrations,
  ZLinkSpotTimerRegistry
} from './spot-timer';
import { createEntrySpotContext } from './spot-context';
import type { RequestResult } from '@zlink-systems/zlink';
import {
  replayActorHandoffBacklog,
  type ZLinkActorHandoffPacket,
  type ZLinkActorHandoffResult
} from '../actors/actor-handoff';
import type {
  ZLinkEntryActorRuntime,
  ZLinkSpotActorTransferRuntime,
  ZLinkSpotActorHandoffRuntime,
  ZLinkSpotBoundSessionRuntime
} from './spot-runtime-ports';

interface ZLinkEntrySpotActivationOptions {
  readonly entrySpotType: Type<ZLinkEntrySpot>;
  readonly timerHandlers?: readonly ZLinkEntrySpotTimerHandlerRegistration[];
  readonly packetHandlers?: readonly ZLinkEntrySpotPacketHandlerRegistration[];
  readonly subscriptionHandlers?: readonly ZLinkEntrySpotSubscriptionHandlerRegistration[];
  readonly actorSendHandlers?: readonly ZLinkEntrySpotActorSendHandlerRegistration[];
  readonly actorRequestHandlers?: readonly ZLinkEntrySpotActorRequestHandlerRegistration[];
  readonly nativeSpot: ZLinkBackendSpot;
  readonly nativeNode: ZLinkBackendSpotNode;
  readonly nodeRid: RoutingId;
  readonly spotNodeName: string;
  readonly channelClient?: ZLinkChannelClient;
  readonly fanoutClient?: ZLinkFanoutClient;
  readonly spotPublisherClient?: ZLinkSpotPublisherClient;
  readonly routedTransport?: ZLinkSpotRoutedTransport;
  readonly spotRouterChannelIdForMesh?: (meshName: string) => string;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
  readonly workerRuntime?: ZLinkSpotWorkerRuntime;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly entryActorRuntime?: ZLinkEntryActorRuntime;
  readonly actorTransferRuntime?: ZLinkSpotActorTransferRuntime;
  readonly boundSessionRuntime?: ZLinkSpotBoundSessionRuntime;
  readonly actorHandoffRuntime?: ZLinkSpotActorHandoffRuntime;
  readonly detachedTaskRunner?: ZLinkDetachedTaskRunner;
}

export class ZLinkEntrySpotActivation {
  private readonly serial = new ZLinkSpotSerialExecutor();
  private readonly actorPacketMailboxes = new ZLinkActorDispatchMailboxSet();
  private readonly timers: ZLinkSpotTimerRegistry;
  private readonly actorHandlers = new ZLinkSpotActorHandlerRegistryRuntime();
  private readonly handlers = new DefaultZLinkSpotHandlerRegistry(this.actorHandlers);
  private readonly outbound: DefaultZLinkSpotOutbound;
  private readonly workerRuntime: ZLinkSpotWorkerRuntime;
  private initialized = false;
  private disposed = false;
  private actorDispatch?: ZLinkSpotActorJoinDispatch;

  entrySpot: ZLinkEntrySpot;
  readonly context: ZLinkEntrySpotContext;

  constructor(private readonly options: ZLinkEntrySpotActivationOptions) {
    // Entry Spot lifecycle, timers and detached continuations use this serial
    // executor. Actor packets use the target actor mailbox.
    this.outbound = new DefaultZLinkSpotOutbound(
      this.serial,
      options.channelClient,
      options.fanoutClient,
      options.spotPublisherClient,
      options.routedTransport,
      options.spotRouterChannelIdForMesh ?? ((meshName) => meshName)
    );
    this.timers = new ZLinkSpotTimerRegistry(
      options.metrics,
      () => options.dispatchErrors?.flow.flowCreationEnabled() ?? true
    );
    this.workerRuntime = options.workerRuntime ?? new ZLinkSpotWorkerRuntime();
    this.context = createEntrySpotContext({
      nativeSpotRid: options.nativeSpot.routingId,
      nodeRid: options.nodeRid,
      handlers: this.handlers,
      outbound: this.outbound,
      timers: this.timers,
      serial: this.serial,
      getEntrySpot: () => this.entrySpot,
      spotNodeName: options.spotNodeName,
      providerResolver: options.providerResolver,
      runtimeEventPublisher: options.runtimeEventPublisher,
      workerRuntime: this.workerRuntime,
      destroyActor: options.entryActorRuntime === undefined
        ? undefined
        : (nodeRid, actor, signal) => options.entryActorRuntime!.destroyActor(
          options.nativeNode,
          nodeRid,
          actor,
          signal
        )
    });
    this.entrySpot = undefined as unknown as ZLinkEntrySpot;
    applyEntrySpotHandlerRegistrations(this.handlers, options.entrySpotType, {
      actorSendHandlers: options.actorSendHandlers,
      actorRequestHandlers: options.actorRequestHandlers,
      packetHandlers: options.packetHandlers,
      subscriptionHandlers: options.subscriptionHandlers
    });
  }

  /**
   * The Entry Spot serial dispatch line for Entry Spot-owned callbacks such as
   * lifecycle, timers, request continuations and worker completions. Actor
   * packets use the target actor mailbox instead.
   */
  get serialExecutor(): ZLinkSpotSerialExecutor {
    return this.serial;
  }

  get nodeRid(): RoutingId {
    return this.options.nodeRid;
  }

  async create(): Promise<void> {
    const entrySpot = await createProviderInstance(this.options.entrySpotType, this.options.providerResolver, this.context);
    this.entrySpot = entrySpot;
    Object.defineProperty(this.entrySpot, 'context', {
      configurable: true,
      enumerable: false,
      value: this.context
    });
    await addEntrySpotTimerRegistrations(
      this.timers,
      this.options.entrySpotType,
      this.entrySpot,
      this.serial,
      { timerHandlers: this.options.timerHandlers },
      {
        providerResolver: this.options.providerResolver,
        spotNodeName: this.options.spotNodeName,
        nodeRid: this.options.nodeRid,
        runtimeEventPublisher: this.options.runtimeEventPublisher
      }
    );
  }

  async configure(): Promise<void> {
    await this.entrySpot.configure?.();
  }

  async initialize(): Promise<void> {
    await this.serial.execute(() => this.entrySpot.onInitialize?.());
    this.attachActorJoinDispatch();
    this.initialized = true;
    this.options.metrics?.change('zlink.spot.count', 1, { kind: 'entry' });
    this.options.metrics?.count('zlink.spot.created', 1, { kind: 'entry' });
  }

  async dispose(): Promise<void> {
    if (this.disposed) return;
    this.disposed = true;
    const errors: unknown[] = [];
    const cleanup = async (operation: () => Promise<void> | void) => {
      try {
        await operation();
      } catch (error) {
        errors.push(error);
      }
    };
    if (this.initialized) {
      await cleanup(() => this.serial.execute(() => this.entrySpot.onClosing?.()));
    }
    await cleanup(() => this.actorDispatch?.dispose());
    await cleanup(() => this.timers.dispose());
    await cleanup(() => this.options.nativeSpot.dispose());
    if (this.initialized) {
      this.options.metrics?.change('zlink.spot.count', -1, { kind: 'entry' });
      this.options.metrics?.count('zlink.spot.closed', 1, { kind: 'entry' });
    }
    if (errors.length === 1) throw errors[0];
    if (errors.length > 1) throw new AggregateError(errors, 'Entry Spot cleanup failed.');
  }

  notifyCreateActor(actor: ZLinkActor, createRequest: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    return this.serial.execute(() => this.entrySpot.onCreateActor?.(actor, createRequest));
  }

  notifyJoinActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    return this.serial.execute(() => this.entrySpot.onJoinedActor(actor));
  }

  /**
   * Entry Spot join admission uses the shared core round-trip. Re-entry defaults
   * to accept when the Entry Spot does not declare `onActorJoin` (actor returning
   * home), so `defaultAccept` is true.
   */
  private attachActorJoinDispatch(): void {
    const dispatch = new ZLinkSpotActorJoinDispatch({
      nativeSpot: this.options.nativeSpot,
      serial: this.serial,
      actors: {
        resolveActor: (actorId) => this.options.entryActorRuntime?.resolveActor(actorId),
        getTarget: () => this.entrySpot,
        defaultAccept: true,
        transfer: this.options.actorTransferRuntime === undefined ? { kind: 'disabled' } : {
          kind: 'enabled',
          runtime: this.options.actorTransferRuntime
        },
        commitNativeActor: (actor) => this.commitEntryActorTransaction(actor),
        commitTransferredActor: async (actor, backlog) => {
          await this.commitEntryActorTransaction(actor);
          return await this.replayActorBacklog(actor, backlog);
        }
      },
      packets: {
        handle: (actorId, parts, returnResponse, remoteBoundSessionTarget, fallbackActorRef) =>
          this.dispatchActorPacket(actorId, parts, returnResponse, remoteBoundSessionTarget, fallbackActorRef),
        bindRemoteSession: (actor, sourceNodeRid, sourceSessionRid) => {
          if (routingIdsEqual(sourceNodeRid, this.options.nativeNode.routingId)) {
            return;
          }
          const target = this.options.boundSessionRuntime?.resolveRemoteBoundSessionTarget(sourceNodeRid, sourceSessionRid);
          if (target !== undefined) {
            this.options.boundSessionRuntime?.rememberRemoteBoundSessionTarget(actor.actorId, target);
          }
          this.options.nativeNode.bindRemoteActorSession(actor, sourceNodeRid, sourceSessionRid);
        },
        replyNoBind: (info, parts, result) => this.replyActorNoBind(info, parts, result)
      },
      boundSessionRuntime: this.options.boundSessionRuntime,
      messageSerializers: this.options.messageSerializers,
      providerResolver: this.options.providerResolver,
      dispatchErrors: this.options.dispatchErrors,
      detachedTaskRunner: this.options.detachedTaskRunner
    });
    dispatch.configureSubscriptions(this.handlers.snapshot());
    dispatch.attach();
    this.actorDispatch = dispatch;
  }

  private async commitEntryActorTransaction(actor: ZLinkActor): Promise<void> {
    const notifyJoined = () => this.serial.execute(() => this.entrySpot.onJoinedActor(actor));
    const runtime = this.options.entryActorRuntime;
    if (runtime === undefined) {
      await notifyJoined();
      return;
    }
    await runtime.commitActorTransaction(actor, notifyJoined);
  }

  notifyLeaveActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    return this.serial.execute(() => this.entrySpot.onLeaveActor(actor));
  }

  notifyDisconnectActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    return this.serial.execute(() => this.entrySpot.onDisconnectActor?.(actor));
  }

  async dispatchActorPacket(
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    const handoff = this.options.actorHandoffRuntime?.capture(
      actorId,
      parts,
      returnResponse,
      remoteBoundSessionTarget,
      fallbackActorRef
    );
    if (handoff !== undefined) return await handoff;
    return this.actorPacketMailboxes.submit(actorId, () =>
      this.dispatchActorPacketInsideMailbox(
        actorId,
        parts,
        returnResponse,
        remoteBoundSessionTarget,
        fallbackActorRef
      ));
  }

  private async replayActorBacklog(
    actor: ZLinkActor,
    backlog: readonly ZLinkActorHandoffPacket[]
  ): Promise<readonly ZLinkActorHandoffResult[]> {
    return await replayActorHandoffBacklog(
      backlog,
      (parts, returnResponse, remoteBoundSessionTarget, fallbackActorRef) =>
        this.actorPacketMailboxes.submit(actor.actorId, () =>
          this.dispatchActorPacketInsideMailbox(
            actor.actorId,
            parts,
            returnResponse,
            remoteBoundSessionTarget,
            fallbackActorRef
          )),
      (index) => this.options.runtimeEventPublisher?.publish({
          sourceName: 'zlink.framework.actor-handoff',
          timestamp: new Date(),
          marker: 'backlog_enqueued',
          actorId: actor.actorId,
          index
        })
    );
  }

  private async dispatchActorPacketInsideMailbox(
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    return new ZLinkSpotActorPacketDispatch({
      spot: this.entrySpot as unknown as ZLinkSpot,
      spotRid: () => String(this.options.nativeSpot.routingId),
      registry: this.actorHandlers,
      resolveActor: (targetActorId) => this.options.entryActorRuntime?.resolveActor(targetActorId),
      routeBeforeLocal: (targetActorId, targetParts, targetReturnResponse, targetRemoteBoundSessionTarget) =>
        this.options.entryActorRuntime?.routePacket(
          targetActorId,
          targetParts,
          targetReturnResponse,
          targetRemoteBoundSessionTarget
        ),
      onRemoteBoundSessionTarget: (targetActorId, target) =>
        this.options.boundSessionRuntime?.rememberRemoteBoundSessionTarget(targetActorId, target),
      onDisconnectActor: (actor) => this.notifyDisconnectActor(actor),
      actorResponseSender: this.options.boundSessionRuntime?.sendActorResponse.bind(this.options.boundSessionRuntime),
      actorErrorSender: this.options.boundSessionRuntime?.sendActorError.bind(this.options.boundSessionRuntime),
      providerResolver: this.options.providerResolver,
      messageSerializers: this.options.messageSerializers,
      dispatchErrors: this.options.dispatchErrors
    }).dispatch(actorId, parts, returnResponse, remoteBoundSessionTarget, fallbackActorRef);
  }

  private replyActorNoBind(
    info: ZLinkBackendActorRecvInfo,
    parts: readonly Message[],
    result: RequestResult
  ): void {
    this.options.nativeNode.replyActorNoBind(info, parts, result);
  }
}
