import type {
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkSpot
} from '../../contracts';
import {
  ZLinkSpotCloseReason,
  ZLinkUserSpotExecutionMode
} from '../../contracts';
import { ZLinkActorDispatchMailboxSet } from '../actors';
import type { ZLinkBackendSpot } from '../backend/contracts';
import type { ZLinkSpotActorHandlerRegistryRuntime } from '../actors';
import type { DefaultZLinkSpotHandlerRegistry } from './spot-handler-registry';
import { ZLinkSpotSerialExecutor } from './spot-serial-executor';
import type { ZLinkSpotTimerRegistry } from './spot-timer';
import type { ZLinkSpotActorJoinDispatch } from './spot-actor-join-dispatch';
import {
  ZLinkExecutionBarrier,
  type ZLinkExecutionBarrierSeal
} from '../execution';
import type { ZLinkTimerRelocationState } from './spot-timer';

export interface ZLinkSpotActivationOptions {
  readonly meshName: string;
  readonly spotId: RoutingId;
  readonly spotType: Type<ZLinkSpot>;
  readonly spot: ZLinkSpot;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly executionMode?: ZLinkUserSpotExecutionMode;
  readonly timers: ZLinkSpotTimerRegistry;
  readonly actorHandlers: ZLinkSpotActorHandlerRegistryRuntime;
  readonly handlers: DefaultZLinkSpotHandlerRegistry;
  readonly externalActorCount?: () => number;
  readonly nativeSpot?: ZLinkBackendSpot;
  readonly closeWhenReady?: (reason: ZLinkSpotCloseReason) => void;
  readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
  readonly executionBarrier?: ZLinkExecutionBarrier;
  actorDispatch?: ZLinkSpotActorJoinDispatch;
}

export interface ZLinkSpotRelocationCapture {
  readonly seal: ZLinkExecutionBarrierSeal;
  readonly timers: readonly ZLinkTimerRelocationState[];
}

export class ZLinkSpotActivation {
  readonly meshName: string;
  readonly spotId: RoutingId;
  readonly spotType: Type<ZLinkSpot>;
  readonly spot: ZLinkSpot;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly executionMode: ZLinkUserSpotExecutionMode;
  readonly timers: ZLinkSpotTimerRegistry;
  readonly actorHandlers: ZLinkSpotActorHandlerRegistryRuntime;
  readonly handlers: DefaultZLinkSpotHandlerRegistry;
  readonly nativeSpot?: ZLinkBackendSpot;
  readonly executionBarrier: ZLinkExecutionBarrier;
  actorDispatch?: ZLinkSpotActorJoinDispatch;

  private readonly joinedActors = new Map<string, ZLinkActor>();
  private readonly actorClaims: ZLinkActorDispatchMailboxSet;
  private readonly actorSerials = new Map<string, ZLinkSpotSerialExecutor>();
  private readonly departedActorIds = new Set<string>();
  private readonly externalActorCount: () => number;
  private readonly closeWhenReady?: (reason: ZLinkSpotCloseReason) => void;
  private readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
  private closeRequested = false;
  private drainCloseRequested = false;
  private drainCloseReason = ZLinkSpotCloseReason.HostShutdown;

  constructor(options: ZLinkSpotActivationOptions) {
    this.meshName = options.meshName;
    this.spotId = options.spotId;
    this.spotType = options.spotType;
    this.spot = options.spot;
    this.serial = options.serial;
    this.executionMode =
      options.executionMode ?? ZLinkUserSpotExecutionMode.SpotWide;
    this.executionBarrier = options.executionBarrier ?? new ZLinkExecutionBarrier();
    this.serial.setExecutionBarrier(this.executionBarrier);
    if (typeof options.timers.setExecutionBarrier === 'function') {
      options.timers.setExecutionBarrier(this.executionBarrier);
    }
    this.actorClaims = new ZLinkActorDispatchMailboxSet(options.metrics);
    this.timers = options.timers;
    this.actorHandlers = options.actorHandlers;
    this.handlers = options.handlers;
    this.externalActorCount = options.externalActorCount ?? (() => 0);
    this.nativeSpot = options.nativeSpot;
    this.closeWhenReady = options.closeWhenReady;
    this.metrics = options.metrics;
    this.actorDispatch = options.actorDispatch;
  }

  executeActor<T>(
    actorId: string,
    operation: (serial: ZLinkSpotSerialExecutor) => Promise<T> | T
  ): Promise<T> {
    return this.actorClaims.submit(actorId, () =>
      operation(this.actorSerial(actorId)));
  }

  sealExecution(): ZLinkExecutionBarrierSeal {
    return this.executionBarrier.seal();
  }

  waitForExecutionQuiescence(
    seal: ZLinkExecutionBarrierSeal,
    signal?: AbortSignal
  ): Promise<void> {
    return this.executionBarrier.waitForQuiescence(seal, signal);
  }

  abortExecutionSeal(seal: ZLinkExecutionBarrierSeal): boolean {
    return this.executionBarrier.abort(seal);
  }

  commitExecutionSeal(seal: ZLinkExecutionBarrierSeal): boolean {
    return this.executionBarrier.commit(seal);
  }

  async captureRelocation(signal?: AbortSignal): Promise<ZLinkSpotRelocationCapture> {
    const seal = this.sealExecution();
    try {
      await this.waitForExecutionQuiescence(seal, signal);
      return {
        seal,
        timers: await this.timers.captureRelocation()
      };
    } catch (error) {
      this.abortExecutionSeal(seal);
      throw error;
    }
  }

  abortRelocation(capture: ZLinkSpotRelocationCapture): boolean {
    if (!this.executionBarrier.isCurrent(capture.seal)) return false;
    this.timers.abortRelocation(capture.timers);
    return this.abortExecutionSeal(capture.seal);
  }

  async commitRelocation(capture: ZLinkSpotRelocationCapture): Promise<boolean> {
    if (!this.commitExecutionSeal(capture.seal)) return false;
    await this.timers.commitRelocation();
    return true;
  }

  resolveJoinedActor(actorId: string): ZLinkActor | undefined {
    return this.joinedActors.get(actorId);
  }

  hasDepartedActor(actorId: string): boolean {
    return this.departedActorIds.has(actorId);
  }

  commitActorJoin(actor: ZLinkActor): () => void {
    const previousActor = this.joinedActors.get(actor.actorId);
    const wasDeparted = this.departedActorIds.has(actor.actorId);
    this.departedActorIds.delete(actor.actorId);
    this.joinedActors.set(actor.actorId, actor);
    return () => {
      if (previousActor === undefined) {
        this.joinedActors.delete(actor.actorId);
      } else {
        this.joinedActors.set(actor.actorId, previousActor);
      }
      if (wasDeparted) {
        this.departedActorIds.add(actor.actorId);
      } else {
        this.departedActorIds.delete(actor.actorId);
      }
    };
  }

  beginActorTransfer(actorId: string): void {
    this.departedActorIds.add(actorId);
  }

  cancelActorTransfer(actorId: string): void {
    if (this.joinedActors.has(actorId)) {
      this.departedActorIds.delete(actorId);
    }
  }

  commitActorDeparture(actorId: string): void {
    this.departedActorIds.add(actorId);
    this.joinedActors.delete(actorId);
    this.notifyCloseReady();
  }

  canClose(): boolean {
    return this.joinedActors.size === 0 && (this.closeRequested || this.externalActorCount() === 0);
  }

  requestClose(): void {
    this.closeRequested = true;
  }

  requestDrainClose(reason: ZLinkSpotCloseReason): void {
    this.closeRequested = true;
    this.drainCloseRequested = true;
    this.drainCloseReason = reason;
    this.notifyCloseReady();
  }

  private notifyCloseReady(): void {
    if (this.drainCloseRequested && this.canClose()) {
      this.closeWhenReady?.(this.drainCloseReason);
    }
  }

  private actorSerial(actorId: string): ZLinkSpotSerialExecutor {
    if (this.executionMode === ZLinkUserSpotExecutionMode.SpotWide) {
      return this.serial;
    }
    let serial = this.actorSerials.get(actorId);
    if (serial === undefined) {
      serial = new ZLinkSpotSerialExecutor(this.metrics, 'user', false);
      serial.setExecutionBarrier(this.executionBarrier);
      this.actorSerials.set(actorId, serial);
    }
    return serial;
  }

}
