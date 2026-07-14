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
  ZLinkSpotCreateResult,
  ZLinkSpotCreateResponse,
  ZLinkSpotPublisherClient,
} from '../../contracts';
import type {
  ZLinkSpotActorRequestHandlerRegistration,
  ZLinkSpotActorSendHandlerRegistration,
  ZLinkSpotPacketHandlerRegistration,
  ZLinkSpotSubscriptionHandlerRegistration,
  ZLinkSpotTimerHandlerRegistration
} from '../../contracts/Configuration/RegistrationTypes';
import {
  ZLinkSpotCreateState
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { throwIfAborted } from '../abort';
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
import type { ZLinkSpotActorJoinDispatch, ZLinkDetachedTaskRunner } from './spot-actor-join-dispatch';
import { ZLinkSpotActorAdmissionCoordinator } from './spot-actor-admission-coordinator';
import { ZLinkSpotActivation } from './spot-activation-state';
import type { ZLinkSpotLocationClaim } from './spot-location-claim';
import type {
  ZLinkSpotActorHandoffRuntime,
  ZLinkSpotActorTransferRuntime,
  ZLinkSpotBoundSessionRuntime
} from './spot-runtime-ports';

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
  readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
}

interface UserSpotLocationClaim {
  readonly meshName: string;
}

export class ZLinkSpotActivationLifecycle {
  private readonly actorAdmission: ZLinkSpotActorAdmissionCoordinator;
  private readonly cleanupStates = new WeakMap<ZLinkSpotActivation, {
    closingAttempted: boolean;
    timersDisposed: boolean;
    nativeDisposed: boolean;
    locationReleased: boolean;
    inFlight?: Promise<void>;
  }>();

  constructor(private readonly options: ZLinkSpotActivationLifecycleOptions) {
    this.actorAdmission = new ZLinkSpotActorAdmissionCoordinator(options);
  }

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
    const serial = new ZLinkSpotSerialExecutor(this.options.metrics, 'user');
    const actorHandlers = new ZLinkSpotActorHandlerRegistryRuntime();
    const handlers = new DefaultZLinkSpotHandlerRegistry(actorHandlers);
    applySpotHandlerRegistrations(handlers, spotType, {
      actorSendHandlers: this.options.spotActorSendHandlers,
      actorRequestHandlers: this.options.spotActorRequestHandlers,
      packetHandlers: this.options.spotPacketHandlers,
      subscriptionHandlers: this.options.spotSubscriptionHandlers
    });
    const timers = new ZLinkSpotTimerRegistry(
      this.options.metrics,
      () => this.options.dispatchErrors?.flow.flowCreationEnabled() ?? true
    );
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
      close: async (contextSignal) => {
        // Native callbacks can cross a promise boundary that does not retain
        // the serial turn context. Queue the close before calling the manager
        // so the callback never waits for work behind its own serial turn.
        if (activation?.serial.isExecuting === true && !activation.serial.isCurrentTurn) {
          activation.requestClose();
          const retry = activation.serial.post(() => this.options.closeSpot(spotRid, contextSignal));
          this.options.detachedTaskRunner?.runDetached(`spot close ${String(spotRid)}`, async () => { await retry; });
          if (this.options.detachedTaskRunner === undefined) void retry.catch(() => undefined);
          return true;
        }
        return await this.options.closeSpot(spotRid, contextSignal);
      }
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
        nativeSpot,
        closeWhenReady: () => {
          const close = async () => { await this.options.closeSpot(spotRid); };
          this.options.detachedTaskRunner?.runDetached(`spot drain close ${String(spotRid)}`, close);
          if (this.options.detachedTaskRunner === undefined) void close().catch(() => undefined);
        }
      });
      const nativeDispatch = this.actorAdmission.attachNativeActorJoinDispatch(activation, nativeSpot);
      activation.actorDispatch = nativeDispatch;
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
    return await this.actorAdmission.dispatchActorPacket(
      activation,
      actorId,
      parts,
      returnResponse,
      remoteBoundSessionTarget,
      fallbackActorRef
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
          wrapFrameworkPayloadMessage(request, this.options.messageSerializers)
        );
        if (createResponse?.accepted === false) {
          return;
        }
        await activation.spot.onInitialize?.();
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
    throwIfAborted(signal);
    const state = this.cleanupStates.get(activation) ?? {
      closingAttempted: false,
      timersDisposed: false,
      nativeDisposed: false,
      locationReleased: false
    };
    this.cleanupStates.set(activation, state);
    if (state.inFlight !== undefined) return await state.inFlight;
    state.inFlight = this.runCleanup(activation, locationMeshName, notifyClosing, state)
      .finally(() => { state.inFlight = undefined; });
    return await state.inFlight;
  }

  private async runCleanup(
    activation: ZLinkSpotActivation,
    locationMeshName: string,
    notifyClosing: boolean,
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
      await cleanup(() => activation.spot.onClosing?.(), () => undefined);
    }
    if (!state.timersDisposed) {
      await cleanup(() => activation.timers.dispose(), () => { state.timersDisposed = true; });
    }
    if (!state.nativeDisposed) {
      await cleanup(() => activation.actorDispatch?.dispose(), () => undefined);
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
      message.close();
    }
  }
}
