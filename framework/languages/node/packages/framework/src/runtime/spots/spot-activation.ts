import type {
  ActorRef,
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkMessageSerializer,
  ZLinkProviderResolver,
  ZLinkRuntimeEventPublisher,
  ZLinkSpot,
  ZLinkSpotActorRequestHandlerRegistration,
  ZLinkSpotActorSendHandlerRegistration,
  ZLinkSpotCreateResult,
  ZLinkSpotCreateResponse,
  ZLinkSpotPacketHandlerRegistration,
  ZLinkSpotPublisherClient,
  ZLinkSpotSubscriptionHandlerRegistration,
  ZLinkSpotTimerHandlerRegistration
} from '../../contracts';
import {
  isZLinkMessage,
  ZLinkSpotCreateState
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type {
  ZLinkBackendSpot,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import { ZLinkDispatchErrorReporter } from '../channels';
import {
  ZLinkSpotActorHandlerRegistryRuntime,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import { ZLinkSpotWorkerRuntime } from '../workers';
import {
  decodeFrameworkPayloadMessage,
  encodeFrameworkPayloadMessage,
  wrapFrameworkPayloadMessage
} from '../messaging/payload-codec';
import { createFreshProviderInstance } from './spot-provider';
import { DefaultZLinkSpotOutbound, type ZLinkSpotRoutedTransport } from './spot-outbound';
import {
  applySpotHandlerRegistrations,
  DefaultZLinkSpotHandlerRegistry
} from './spot-handler-registry';
import {
  addSpotTimerRegistrations,
  ZLinkSpotTimerRegistry
} from './spot-timer';
import { ZLinkSpotSerialExecutor } from './spot-serial-executor';
import { createSpotContext } from './spot-context';
import {
  ZLinkSpotActorJoinDispatch,
  type ZLinkDetachedTaskRunner
} from './spot-actor-join-dispatch';
import {
  ZLinkSpotActorPacketDispatch
} from './spot-actor-packet-dispatch';
import { ZLinkSpotActivation } from './spot-activation-state';
import type { ZLinkSpotLocationClaim } from './spot-location-claim';
import type {
  ZLinkNativeActorJoinSnapshot,
  ZLinkSpotActorHandoffRuntime,
  ZLinkSpotActorTransferRuntime,
  ZLinkSpotBoundSessionRuntime
} from './spot-runtime-ports';
import { routingIdsEqual } from '../routing-id';
import {
  replayActorHandoffBacklog,
  type ZLinkActorHandoffPacket,
  type ZLinkActorHandoffResult
} from '../actors/actor-handoff';

export interface ZLinkSpotActivationLifecycleOptions {
  readonly spotTimerHandlers?: readonly ZLinkSpotTimerHandlerRegistration[];
  readonly spotPacketHandlers?: readonly ZLinkSpotPacketHandlerRegistration[];
  readonly spotSubscriptionHandlers?: readonly ZLinkSpotSubscriptionHandlerRegistration[];
  readonly spotActorSendHandlers?: readonly ZLinkSpotActorSendHandlerRegistration[];
  readonly spotActorRequestHandlers?: readonly ZLinkSpotActorRequestHandlerRegistration[];
  readonly nodeRid?: RoutingId;
  readonly nodeRidProvider?: () => RoutingId | undefined;
  readonly actorCountProvider?: (spotRid: RoutingId) => number;
  readonly channelClient?: ZLinkChannelClient;
  readonly fanoutClient?: ZLinkFanoutClient;
  readonly spotPublisherClient?: ZLinkSpotPublisherClient;
  readonly routedTransport?: ZLinkSpotRoutedTransport;
  readonly spotRouterChannelIdForMesh?: (meshName: string) => string;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly workerRuntime: ZLinkSpotWorkerRuntime;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly locationClaim: ZLinkSpotLocationClaim;
  readonly createNativeSpot?: (spotRid: RoutingId) => ZLinkBackendSpot | undefined;
  readonly nativeSpotNodeProvider?: () => ZLinkBackendSpotNode | undefined;
  readonly actorResolver?: (actorId: string) => ZLinkActor | undefined;
  readonly actorTransferRuntime?: ZLinkSpotActorTransferRuntime;
  readonly boundSessionRuntime?: ZLinkSpotBoundSessionRuntime;
  readonly actorHandoffRuntime?: ZLinkSpotActorHandoffRuntime;
  readonly detachedTaskRunner?: ZLinkDetachedTaskRunner;
  readonly leaveActor: (spotRid: RoutingId, actor: ZLinkActor, signal?: AbortSignal) => Promise<void>;
  readonly closeSpot: (spotRid: RoutingId, signal?: AbortSignal) => Promise<boolean>;
  readonly registerActivation: (activation: ZLinkSpotActivation) => void;
}

interface UserSpotLocationClaim {
  readonly meshName: string;
}

export class ZLinkSpotActivationLifecycle {
  private readonly cleanupStates = new WeakMap<ZLinkSpotActivation, {
    closingAttempted: boolean;
    timersDisposed: boolean;
    nativeDisposed: boolean;
    locationReleased: boolean;
    inFlight?: Promise<void>;
  }>();

  constructor(private readonly options: ZLinkSpotActivationLifecycleOptions) {}

  resourcesReleased(activation: ZLinkSpotActivation): boolean {
    const state = this.cleanupStates.get(activation);
    return state?.timersDisposed === true &&
      state.nativeDisposed === true &&
      state.locationReleased === true;
  }

  async create<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    request: Message,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    const serial = new ZLinkSpotSerialExecutor();
    const actorHandlers = new ZLinkSpotActorHandlerRegistryRuntime();
    const handlers = new DefaultZLinkSpotHandlerRegistry(actorHandlers);
    applySpotHandlerRegistrations(handlers, spotType, {
      actorSendHandlers: this.options.spotActorSendHandlers,
      actorRequestHandlers: this.options.spotActorRequestHandlers,
      packetHandlers: this.options.spotPacketHandlers,
      subscriptionHandlers: this.options.spotSubscriptionHandlers
    });
    const timers = new ZLinkSpotTimerRegistry();
    let nativeSpot: ZLinkBackendSpot | undefined;
    const outbound = new DefaultZLinkSpotOutbound(
      serial,
      this.options.channelClient,
      this.options.fanoutClient,
      this.options.spotPublisherClient,
      this.options.routedTransport,
      this.options.spotRouterChannelIdForMesh ?? ((meshName) => meshName),
      () => nativeSpot
    );
    const locationClaim = await this.options.locationClaim.claimUserSpot(spotRid, spotType.name);
    if (!locationClaim.claimed) {
      return { spotRid, state: ZLinkSpotCreateState.Existing };
    }

    let spot: ZLinkSpot | undefined;
    let activation: ZLinkSpotActivation | undefined;
    let lifecycleStarted = false;
    const context = createSpotContext({
      spotRid,
      handlers,
      outbound,
      timers,
      serial,
      getSpot: () => spot,
      nodeRid: this.options.nodeRid,
      nodeRidProvider: this.options.nodeRidProvider,
      providerResolver: this.options.providerResolver,
      runtimeEventPublisher: this.options.runtimeEventPublisher,
      workerRuntime: this.options.workerRuntime,
      leaveActor: (actor, contextSignal) => this.options.leaveActor(spotRid, actor, contextSignal),
      close: (contextSignal) => this.options.closeSpot(spotRid, contextSignal)
    });
    try {
      spot = await createFreshProviderInstance(spotType, this.options.providerResolver, context);
      Object.defineProperty(spot, 'context', {
        configurable: true,
        enumerable: false,
        value: context
      });

      // getOrCreateSpot registers the native Spot under this rid so core routes
      // actor-join admission requests to it (createSpot alone does not register).
      nativeSpot = this.options.createNativeSpot?.(spotRid);
      activation = new ZLinkSpotActivation({
        spotRid,
        spotType,
        spot,
        serial,
        timers,
        actorHandlers,
        handlers,
        externalActorCount: () => this.options.actorCountProvider?.(spotRid) ?? 0,
        nativeSpot
      });
      const nativeDispatch = this.attachNativeActorJoinDispatch(activation, nativeSpot);
      lifecycleStarted = true;
      return await this.runCreateLifecycle(activation, spotType, request, locationClaim, nativeDispatch, signal);
    } catch (error) {
      const cleanupErrors: unknown[] = [];
      try {
        if (activation !== undefined) {
          await this.cleanupActivation(activation, locationClaim.meshName, lifecycleStarted, signal);
        } else {
          const partialCleanup = await Promise.allSettled([
            timers.dispose(),
            nativeSpot?.dispose(),
            this.options.locationClaim.release(locationClaim.meshName, spotRid)
          ]);
          const partialErrors = partialCleanup
            .filter((result): result is PromiseRejectedResult => result.status === 'rejected')
            .map((result) => result.reason);
          if (partialErrors.length === 1) throw partialErrors[0];
          if (partialErrors.length > 1) {
            throw new AggregateError(partialErrors, `Spot '${spotRid}' partial creation cleanup failed.`);
          }
        }
      } catch (cleanupError) {
        cleanupErrors.push(cleanupError);
      }
      if (cleanupErrors.length > 0) {
        throw new AggregateError([error, ...cleanupErrors], `Spot '${spotRid}' creation cleanup failed.`);
      }
      throw error;
    }
  }

  async close(activation: ZLinkSpotActivation, signal?: AbortSignal): Promise<void> {
    await activation.serial.execute(() => this.closeInsideSerial(activation, signal));
  }

  async closeInsideSerial(activation: ZLinkSpotActivation, signal?: AbortSignal): Promise<void> {
    await this.cleanupActivation(
      activation,
      this.options.locationClaim.meshNameForRelease(),
      true,
      signal
    );
  }

  async dispatchActorPacket(
    activation: ZLinkSpotActivation,
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
    return await this.dispatchActorPacketDirect(
      activation,
      actorId,
      parts,
      returnResponse,
      remoteBoundSessionTarget,
      fallbackActorRef
    );
  }

  private async dispatchActorPacketDirect(
    activation: ZLinkSpotActivation,
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    return new ZLinkSpotActorPacketDispatch({
      spot: activation.spot,
      spotRid: () => String(activation.spotRid),
      registry: activation.actorHandlers,
      serial: activation.serial,
      resolveActor: (targetActorId) => activation.hasDepartedActor(targetActorId)
        ? undefined
        : activation.resolveJoinedActor(targetActorId) ?? this.options.actorResolver?.(targetActorId),
      actorLeft: (targetActorId) => activation.hasDepartedActor(targetActorId),
      onRemoteBoundSessionTarget: (targetActorId, target) =>
        this.options.boundSessionRuntime?.rememberRemoteBoundSessionTarget(targetActorId, target),
      onDisconnectActor: (actor) =>
        activation.serial.execute(() => activation.spot.onDisconnectActor?.(actor)),
      actorResponseSender: this.options.boundSessionRuntime?.sendActorResponse.bind(this.options.boundSessionRuntime),
      actorErrorSender: this.options.boundSessionRuntime?.sendActorError.bind(this.options.boundSessionRuntime),
      providerResolver: this.options.providerResolver,
      messageSerializers: this.options.messageSerializers,
      dispatchErrors: this.options.dispatchErrors
    }).dispatch(actorId, parts, returnResponse, remoteBoundSessionTarget, fallbackActorRef);
  }

  private attachNativeActorJoinDispatch(
    activation: ZLinkSpotActivation,
    nativeSpot: ZLinkBackendSpot | undefined
  ): ZLinkSpotActorJoinDispatch | undefined {
    if (nativeSpot === undefined) {
      return undefined;
    }
    const nativeDispatch = new ZLinkSpotActorJoinDispatch({
      nativeSpot,
      serial: activation.serial,
      actors: {
        resolveActor: (actorId) => activation.hasDepartedActor(actorId)
          ? undefined
          : activation.resolveJoinedActor(actorId) ?? this.options.actorResolver?.(actorId),
        getTarget: () => activation.spot,
        defaultAccept: false,
        transfer: this.options.actorTransferRuntime === undefined ? { kind: 'disabled' } : {
          kind: 'enabled',
          runtime: this.options.actorTransferRuntime
        },
        commitNativeActor: async (actor) => {
          await this.commitNativeActorTransaction(activation, actor);
        },
        commitTransferredActor: (actor, backlog) => this.commitTransferredActorTransaction(activation, actor, backlog)
      },
      packets: {
        handle: (actorId, parts, returnResponse, remoteBoundSessionTarget, fallbackActorRef) =>
          this.dispatchActorPacket(
            activation,
            actorId,
            parts,
            returnResponse,
            remoteBoundSessionTarget,
            fallbackActorRef
          ),
        bindRemoteSession: (actor, sourceNodeRid, sourceSessionRid) => {
          const node = this.options.nativeSpotNodeProvider?.();
          if (node === undefined || routingIdsEqual(sourceNodeRid, node.routingId)) {
            return;
          }
          const target = this.options.boundSessionRuntime?.resolveRemoteBoundSessionTarget(sourceNodeRid, sourceSessionRid);
          if (target !== undefined) {
            this.options.boundSessionRuntime?.rememberRemoteBoundSessionTarget(actor.actorId, target);
          }
          node.bindRemoteActorSession(actor, sourceNodeRid, sourceSessionRid);
        },
        replyNoBind: (info, parts, result) =>
          this.options.nativeSpotNodeProvider?.()?.replyActorNoBind(info, parts, result)
      },
      boundSessionRuntime: this.options.boundSessionRuntime,
      messageSerializers: this.options.messageSerializers,
      providerResolver: this.options.providerResolver,
      dispatchErrors: this.options.dispatchErrors,
      detachedTaskRunner: this.options.detachedTaskRunner
    });
    nativeDispatch.attach();
    return nativeDispatch;
  }

  private async commitNativeActorTransaction(
    activation: ZLinkSpotActivation,
    actor: ZLinkActor
  ): Promise<void> {
    const transfer = this.options.actorTransferRuntime;
    const spotMeshName = this.options.locationClaim.meshNameForRelease();
    let snapshot: ZLinkNativeActorJoinSnapshot | undefined;
    let rollbackMembership: (() => void) | undefined;
    try {
      snapshot = await transfer?.claimNativeActorLocation(
        actor,
        activation.spotRid,
        spotMeshName
      );
      transfer?.commitRoutedActor(actor, activation.spotRid, activation.spot);
      rollbackMembership = activation.commitActorJoin(actor);
      await activation.serial.execute(() => activation.spot.onJoinedActor?.(actor));
      await transfer?.publishRoutedActorOwnership(actor);
    } catch (error) {
      rollbackMembership?.();
      try {
        if (snapshot !== undefined) {
          await transfer?.rollbackNativeActorJoin(actor, snapshot);
        }
      } catch (rollbackError) {
        throw new AggregateError([error, rollbackError], 'Native actor admission and rollback both failed.');
      }
      throw error;
    }
  }

  private async commitTransferredActorTransaction(
    activation: ZLinkSpotActivation,
    actor: ZLinkActor,
    backlog: readonly ZLinkActorHandoffPacket[]
  ): Promise<readonly ZLinkActorHandoffResult[]> {
    const transfer = this.options.actorTransferRuntime;
    try {
      await transfer?.claimRoutedActorLocation(
        actor,
        activation.spotRid,
        this.options.locationClaim.meshNameForRelease()
      );
      transfer?.commitRoutedActor(actor, activation.spotRid, activation.spot);
      activation.commitActorJoin(actor);
      await activation.serial.execute(() => activation.spot.onJoinedActor?.(actor));
      const results = backlog.length === 0
        ? []
        : await this.replayActorBacklog(activation, actor, backlog);
      await transfer?.publishRoutedActorOwnership(actor);
      return results;
    } catch (error) {
      activation.commitActorDeparture(actor.actorId);
      transfer?.clearRoutedActor(actor);
      try {
        await transfer?.rollbackRoutedActor(actor);
      } catch (rollbackError) {
        throw new AggregateError([error, rollbackError], 'Actor admission and rollback both failed.');
      }
      throw error;
    }
  }

  private async replayActorBacklog(
    activation: ZLinkSpotActivation,
    actor: ZLinkActor,
    backlog: readonly ZLinkActorHandoffPacket[]
  ): Promise<readonly ZLinkActorHandoffResult[]> {
    return await replayActorHandoffBacklog(
      backlog,
      (parts, returnResponse, remoteBoundSessionTarget, fallbackActorRef) =>
        this.dispatchActorPacketDirect(
          activation,
          actor.actorId,
          parts,
          returnResponse,
          remoteBoundSessionTarget,
          fallbackActorRef
        ),
      (index) => this.options.runtimeEventPublisher?.publish({
          sourceName: 'zlink.framework.actor-handoff',
          timestamp: new Date(),
          marker: 'backlog_enqueued',
          actorId: actor.actorId,
          index
        })
    );
  }

  private async runCreateLifecycle<TSpot extends ZLinkSpot>(
    activation: ZLinkSpotActivation,
    spotType: Type<TSpot>,
    request: Message,
    locationClaim: UserSpotLocationClaim,
    nativeDispatch: ZLinkSpotActorJoinDispatch | undefined,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    try {
      await activation.spot.configure?.();
      nativeDispatch?.configureSubscriptions(activation.handlers.snapshot());
      await addSpotTimerRegistrations(
        activation.timers,
        spotType,
        activation.spotRid,
        activation.spot,
        activation.serial,
        { timerHandlers: this.options.spotTimerHandlers },
        {
          providerResolver: this.options.providerResolver,
          runtimeEventPublisher: this.options.runtimeEventPublisher,
          signal
        }
      );
      let createResponse: ZLinkSpotCreateResponse | undefined;
      await activation.serial.execute(async () => {
        createResponse = await activation.spot.onCreate?.(
          wrapFrameworkPayloadMessage(request, this.options.messageSerializers),
          signal
        );
        if (createResponse?.accepted === false) {
          return;
        }
        await activation.spot.onInitialize?.(signal);
      });
      if (createResponse?.accepted === false) {
        await this.cleanupActivation(activation, locationClaim.meshName, false, signal);
        return {
          spotRid: activation.spotRid,
          state: ZLinkSpotCreateState.Rejected,
          reply: this.decodeCreateReply(createResponse.reply)
        };
      }
      this.options.registerActivation(activation);
      return {
        spotRid: activation.spotRid,
        state: ZLinkSpotCreateState.Created,
        reply: this.decodeCreateReply(createResponse?.reply)
      };
    } catch (error) {
      try {
        await this.cleanupActivation(activation, locationClaim.meshName, true, signal);
      } catch (cleanupError) {
        throw new AggregateError([error, cleanupError], `Spot '${activation.spotRid}' creation cleanup failed.`);
      }
      throw error;
    }
  }

  private async cleanupActivation(
    activation: ZLinkSpotActivation,
    locationMeshName: string,
    notifyClosing: boolean,
    signal?: AbortSignal
  ): Promise<void> {
    const state = this.cleanupStates.get(activation) ?? {
      closingAttempted: false,
      timersDisposed: false,
      nativeDisposed: false,
      locationReleased: false
    };
    this.cleanupStates.set(activation, state);
    if (state.inFlight !== undefined) return await state.inFlight;
    state.inFlight = this.runCleanup(activation, locationMeshName, notifyClosing, signal, state)
      .finally(() => { state.inFlight = undefined; });
    return await state.inFlight;
  }

  private async runCleanup(
    activation: ZLinkSpotActivation,
    locationMeshName: string,
    notifyClosing: boolean,
    signal: AbortSignal | undefined,
    state: {
      closingAttempted: boolean;
      timersDisposed: boolean;
      nativeDisposed: boolean;
      locationReleased: boolean;
    }
  ): Promise<void> {
    const errors: unknown[] = [];
    const cleanup = async (operation: () => Promise<void> | void, completed: () => void) => {
      try {
        await operation();
        completed();
      } catch (error) {
        errors.push(error);
      }
    };
    if (notifyClosing && !state.closingAttempted) {
      state.closingAttempted = true;
      await cleanup(() => activation.spot.onClosing?.(signal), () => undefined);
    }
    if (!state.timersDisposed) {
      await cleanup(() => activation.timers.dispose(), () => { state.timersDisposed = true; });
    }
    if (!state.nativeDisposed) {
      await cleanup(() => activation.nativeSpot?.dispose(), () => { state.nativeDisposed = true; });
    }
    if (!state.locationReleased) {
      await cleanup(
        () => this.options.locationClaim.release(locationMeshName, activation.spotRid),
        () => { state.locationReleased = true; }
      );
    }
    if (errors.length === 1) throw errors[0];
    if (errors.length > 1) {
      throw new AggregateError(errors, `Spot '${activation.spotRid}' cleanup failed.`);
    }
  }

  private decodeCreateReply(reply: unknown): unknown {
    if (reply === undefined) {
      return undefined;
    }
    const message = encodeFrameworkPayloadMessage(reply, this.options.messageSerializers);
    try {
      return decodeFrameworkPayloadMessage(message, this.options.messageSerializers);
    } finally {
      if (ownsFrameworkPayloadMessage(reply)) {
        message.close();
      }
    }
  }
}

function ownsFrameworkPayloadMessage(value: unknown): boolean {
  return value === undefined || !(isMessage(value) || (isZLinkMessage(value) && value.isEncoded()));
}

function isMessage(value: unknown): value is Message {
  return typeof value === 'object'
    && value !== null
    && typeof (value as { data?: unknown }).data === 'function';
}
