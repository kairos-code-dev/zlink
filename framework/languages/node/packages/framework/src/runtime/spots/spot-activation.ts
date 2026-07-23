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
  ZLinkInstanceSpot,
  ZLinkSpot,
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
  ZLinkSpotCloseReason,
  ZLinkSpotCreateState
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { throwIfAborted } from '../abort';
import { ZLinkConfigurationException } from '../configuration';
import type {
  ZLinkBackendSpot,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import { ZLinkDispatchErrorReporter } from '../channels';
import {
  ZLinkSpotActorHandlerRegistryRuntime,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import { ZLinkWorkerRuntime } from '../workers';
import {
  decodeFrameworkPayloadMessage,
  encodeFrameworkPayloadMessage,
  wrapFrameworkPayloadMessage
} from '../messaging/payload-codec';
import { createFreshProviderInstance } from './spot-provider';
import {
  DefaultZLinkSpotOutbound,
  type ZLinkSpotAddressTransport,
  type ZLinkSpotRoutedTransport
} from './spot-outbound';
import {
  applySpotHandlerRegistrations,
  DefaultZLinkInstanceSpotHandlerRegistry,
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
import type { ZLinkLocalSpotCreateResult } from './spot-manager-internal-contracts';
import { invokeSpotClosing } from './spot-closing';

export interface ZLinkSpotActivationLifecycleOptions {
  readonly spotTimerHandlers?: readonly ZLinkSpotTimerHandlerRegistration[];
  readonly spotPacketHandlers?: readonly ZLinkSpotPacketHandlerRegistration[];
  readonly spotSubscriptionHandlers?: readonly ZLinkSpotSubscriptionHandlerRegistration[];
  readonly spotActorSendHandlers?: readonly ZLinkSpotActorSendHandlerRegistration[];
  readonly spotActorRequestHandlers?: readonly ZLinkSpotActorRequestHandlerRegistration[];
  readonly nodeRid?: RoutingId;
  readonly nodeRidProvider?: (meshName: string) => RoutingId | undefined;
  readonly actorCountProvider?: (spotRid: RoutingId) => number;
  readonly channelClient?: ZLinkChannelClient;
  readonly fanoutClient?: ZLinkFanoutClient;
  readonly spotPublisherClient?: ZLinkSpotPublisherClient;
  readonly routedTransport?: ZLinkSpotRoutedTransport;
  readonly addressTransport?: ZLinkSpotAddressTransport;
  readonly spotRouterChannelIdForMesh?: (meshName: string) => string;
  readonly channelMeshNameForChannel?: (channelName: string) => string | undefined;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly workerRuntime: ZLinkWorkerRuntime;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly locationClaim: ZLinkSpotLocationClaim;
  readonly createNativeSpot?: (meshName: string, spotRid: RoutingId) => ZLinkBackendSpot | undefined;
  readonly nativeSpotNodeProvider?: (meshName: string) => ZLinkBackendSpotNode | undefined;
  readonly actorResolver?: (actorId: string) => ZLinkActor | undefined;
  readonly actorTransferRuntime?: ZLinkSpotActorTransferRuntime;
  readonly boundSessionRuntime?: ZLinkSpotBoundSessionRuntime;
  readonly actorHandoffRuntime?: ZLinkSpotActorHandoffRuntime;
  readonly detachedTaskRunner?: ZLinkDetachedTaskRunner;
  readonly leaveActor: (spotRid: RoutingId, actor: ZLinkActor, signal?: AbortSignal) => Promise<void>;
  readonly closeSpot: (
    meshName: string,
    spotRid: RoutingId,
    signal?: AbortSignal,
    reason?: ZLinkSpotCloseReason
  ) => Promise<boolean>;
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

  async materializeInstance<TSpot extends ZLinkInstanceSpot>(
    meshName: string,
    instanceType: string,
    implementation: Type<TSpot>,
    spotRid: RoutingId,
    signal?: AbortSignal
  ): Promise<ZLinkSpotActivation> {
    const serial = new ZLinkSpotSerialExecutor(this.options.metrics, 'instance');
    const actorHandlers = new ZLinkSpotActorHandlerRegistryRuntime();
    const handlers = new DefaultZLinkSpotHandlerRegistry(actorHandlers);
    const instanceHandlers = new DefaultZLinkInstanceSpotHandlerRegistry(handlers);
    applySpotHandlerRegistrations(handlers, implementation as unknown as Type<ZLinkSpot>, {
      packetHandlers: this.options.spotPacketHandlers
    });
    const timers = new ZLinkSpotTimerRegistry(
      this.options.metrics,
      () => this.options.dispatchErrors?.flow.flowCreationEnabled() ?? true
    );
    const outbound = new DefaultZLinkSpotOutbound(
      serial,
      this.options.channelClient,
      this.options.fanoutClient,
      this.options.spotPublisherClient,
      this.options.routedTransport,
      this.options.spotRouterChannelIdForMesh ?? ((selectedMesh) => selectedMesh),
      undefined,
      meshName,
      undefined,
      this.options.addressTransport
    );
    let instance: TSpot | undefined;
    const context = {
      ...createSpotContext({
        meshName,
        spotRid,
        handlers,
        outbound,
        timers,
        serial,
        getSpot: () => instance as unknown as ZLinkSpot,
        nodeRid: this.options.nodeRid,
        nodeRidProvider: () => this.options.nodeRidProvider?.(meshName),
        providerResolver: this.options.providerResolver,
        runtimeEventPublisher: this.options.runtimeEventPublisher,
        workerRuntime: this.options.workerRuntime,
        close: (contextSignal) => this.options.closeSpot(meshName, spotRid, contextSignal)
      }),
      handlers: instanceHandlers
    };
    instance = await createFreshProviderInstance(
      implementation,
      this.options.providerResolver,
      context
    );
    Object.defineProperty(instance, 'context', {
      configurable: true,
      enumerable: false,
      value: context
    });
    const activation = new ZLinkSpotActivation({
      meshName,
      spotRid,
      spotType: implementation as unknown as Type<ZLinkSpot>,
      spot: instance as unknown as ZLinkSpot,
      serial,
      timers,
      actorHandlers,
      handlers,
      externalActorCount: () => 0,
      closeWhenReady: (reason) => this.scheduleDrainClose(meshName, spotRid, reason)
    });
    try {
      await instance.configure?.();
      await addSpotTimerRegistrations(
        timers,
        implementation as unknown as Type<ZLinkSpot>,
        spotRid,
        instance as unknown as ZLinkSpot,
        serial,
        { timerHandlers: this.options.spotTimerHandlers },
        {
          providerResolver: this.options.providerResolver,
          runtimeEventPublisher: this.options.runtimeEventPublisher,
          signal
        }
      );
      await serial.execute(() => instance!.onInitialize?.());
      this.options.registerActivation(activation);
      return activation;
    } catch (error) {
      await timers.dispose();
      throw new AggregateError(
        [error],
        `Instance Spot '${instanceType}' materialization failed for '${String(spotRid)}'.`
      );
    }
  }

  async discardInstance(activation: ZLinkSpotActivation): Promise<void> {
    const errors: unknown[] = [];
    try {
      await activation.serial.execute(() => invokeSpotClosing(
        activation.spot.onClosing?.bind(activation.spot),
        ZLinkSpotCloseReason.ExplicitClose
      ));
    } catch (error) {
      errors.push(error);
    }
    try {
      await activation.timers.dispose();
    } catch (error) {
      errors.push(error);
    }
    if (errors.length === 1) throw errors[0];
    if (errors.length > 1) {
      throw new AggregateError(
        errors,
        `Instance Spot '${String(activation.spotRid)}' cleanup failed.`
      );
    }
  }

  resourcesReleased(activation: ZLinkSpotActivation): boolean {
    const state = this.cleanupStates.get(activation);
    return state?.timersDisposed === true &&
      state.nativeDisposed === true &&
      state.locationReleased === true;
  }

  async create<TSpot extends ZLinkSpot>(
    meshName: string,
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    request: Message,
    signal?: AbortSignal
  ): Promise<ZLinkLocalSpotCreateResult> {
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
      () => nativeSpot,
      meshName,
      this.options.channelMeshNameForChannel,
      this.options.addressTransport
    );
    // Core owns the lifecycle generation. Publish the location only after the
    // formal Spot exists, so no synthetic generation can escape into routing.
    nativeSpot = this.options.createNativeSpot?.(meshName, spotRid);
    const spotGeneration = nativeSpot?.lifecycleGeneration;
    if (nativeSpot === undefined || spotGeneration === undefined || spotGeneration <= 0n) {
      await nativeSpot?.dispose();
      if (!this.options.locationClaim.enabled) {
        const unclaimed = { claimed: true as const, meshName: '' };
        return await this.createWithoutNativeSpot(
          meshName,
          spotType,
          spotRid,
          request,
          serial,
          actorHandlers,
          handlers,
          timers,
          outbound,
          unclaimed,
          signal
        );
      }
      throw new ZLinkConfigurationException('Core Spot lifecycle generation is not available.');
    }
    const locationClaim = await this.options.locationClaim.claimUserSpot(
      meshName,
      spotRid,
      spotType.name,
      spotGeneration
    );
    if (!locationClaim.claimed) {
      await nativeSpot.dispose();
      return { spotRid, state: ZLinkSpotCreateState.Existing };
    }

    let spot: ZLinkSpot | undefined;
    let activation: ZLinkSpotActivation | undefined;
    let lifecycleStarted = false;
    const context = createSpotContext({
      meshName,
      spotRid,
      handlers,
      outbound,
      timers,
      serial,
      getSpot: () => spot,
      nodeRid: this.options.nodeRid,
      nodeRidProvider: () => this.options.nodeRidProvider?.(meshName),
      providerResolver: this.options.providerResolver,
      runtimeEventPublisher: this.options.runtimeEventPublisher,
      workerRuntime: this.options.workerRuntime,
      close: async (contextSignal) => {
        // Native callbacks can cross a promise boundary that does not retain
        // the serial turn context. Queue the close before calling the manager
        // so the callback never waits for work behind its own serial turn.
        if (activation?.serial.isExecuting === true && !activation.serial.isCurrentTurn) {
          activation.requestClose();
          const retry = activation.serial.post(() => this.options.closeSpot(meshName, spotRid, contextSignal));
          this.options.detachedTaskRunner?.runDetached(`spot close ${String(spotRid)}`, async () => { await retry; });
          if (this.options.detachedTaskRunner === undefined) void retry.catch(() => undefined);
          return true;
        }
        return await this.options.closeSpot(meshName, spotRid, contextSignal);
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
      activation = new ZLinkSpotActivation({
        meshName,
        spotRid,
        spotType,
        spot,
        serial,
        timers,
        actorHandlers,
        handlers,
        externalActorCount: () => this.options.actorCountProvider?.(spotRid) ?? 0,
        nativeSpot,
        closeWhenReady: (reason) => this.scheduleDrainClose(meshName, spotRid, reason)
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
            nativeSpot.dispose(),
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

  private async createWithoutNativeSpot<TSpot extends ZLinkSpot>(
    meshName: string,
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    request: Message,
    serial: ZLinkSpotSerialExecutor,
    actorHandlers: ZLinkSpotActorHandlerRegistryRuntime,
    handlers: DefaultZLinkSpotHandlerRegistry,
    timers: ZLinkSpotTimerRegistry,
    outbound: DefaultZLinkSpotOutbound,
    locationClaim: UserSpotLocationClaim,
    signal?: AbortSignal
  ): Promise<ZLinkLocalSpotCreateResult> {
    let spot: ZLinkSpot | undefined;
    const context = createSpotContext({
      meshName,
      spotRid,
      handlers,
      outbound,
      timers,
      serial,
      getSpot: () => spot,
      nodeRid: this.options.nodeRid,
      nodeRidProvider: () => this.options.nodeRidProvider?.(meshName),
      providerResolver: this.options.providerResolver,
      runtimeEventPublisher: this.options.runtimeEventPublisher,
      workerRuntime: this.options.workerRuntime,
      close: (contextSignal) => this.options.closeSpot(meshName, spotRid, contextSignal)
    });
    spot = await createFreshProviderInstance(spotType, this.options.providerResolver, context);
    Object.defineProperty(spot, 'context', { configurable: true, enumerable: false, value: context });
    const activation = new ZLinkSpotActivation({
      meshName,
      spotRid,
      spotType,
      spot,
      serial,
      timers,
      actorHandlers,
      handlers,
      externalActorCount: () => this.options.actorCountProvider?.(spotRid) ?? 0,
      closeWhenReady: (reason) => this.scheduleDrainClose(meshName, spotRid, reason)
    });
    return await this.runCreateLifecycle(activation, spotType, request, locationClaim, undefined, signal);
  }

  private scheduleDrainClose(
    meshName: string,
    spotRid: RoutingId,
    reason: ZLinkSpotCloseReason
  ): void {
    const close = async () => {
      await this.options.closeSpot(meshName, spotRid, undefined, reason);
    };
    this.options.detachedTaskRunner?.runDetached(`spot drain close ${String(spotRid)}`, close);
    if (this.options.detachedTaskRunner === undefined) {
      void close().catch(() => undefined);
    }
  }

  async close(
    activation: ZLinkSpotActivation,
    signal?: AbortSignal,
    reason = ZLinkSpotCloseReason.ExplicitClose
  ): Promise<void> {
    await activation.serial.execute(() => this.closeInsideSerial(activation, signal, reason));
  }

  async closeInsideSerial(
    activation: ZLinkSpotActivation,
    signal?: AbortSignal,
    reason = ZLinkSpotCloseReason.ExplicitClose
  ): Promise<void> {
    await this.cleanupActivation(
      activation,
      activation.meshName,
      true,
      signal,
      reason
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
  ): Promise<ZLinkLocalSpotCreateResult> {
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
    signal?: AbortSignal,
    reason = ZLinkSpotCloseReason.ExplicitClose
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
    state.inFlight = this.runCleanup(activation, locationMeshName, notifyClosing, reason, state)
      .finally(() => { state.inFlight = undefined; });
    return await state.inFlight;
  }

  private async runCleanup(
    activation: ZLinkSpotActivation,
    locationMeshName: string,
    notifyClosing: boolean,
    reason: ZLinkSpotCloseReason,
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
      await cleanup(() => invokeSpotClosing(
        activation.spot.onClosing?.bind(activation.spot),
        reason
      ), () => undefined);
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
