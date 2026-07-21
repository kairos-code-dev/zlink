import type {
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkSpot
} from '../../contracts';
import type { ZLinkBackendSpot } from '../backend/contracts';
import type { ZLinkSpotActorHandlerRegistryRuntime } from '../actors';
import type { DefaultZLinkSpotHandlerRegistry } from './spot-handler-registry';
import type { ZLinkSpotSerialExecutor } from './spot-serial-executor';
import type { ZLinkSpotTimerRegistry } from './spot-timer';
import type { ZLinkSpotActorJoinDispatch } from './spot-actor-join-dispatch';

export interface ZLinkSpotActivationOptions {
  readonly meshName: string;
  readonly spotRid: RoutingId;
  readonly spotType: Type<ZLinkSpot>;
  readonly spot: ZLinkSpot;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly timers: ZLinkSpotTimerRegistry;
  readonly actorHandlers: ZLinkSpotActorHandlerRegistryRuntime;
  readonly handlers: DefaultZLinkSpotHandlerRegistry;
  readonly externalActorCount?: () => number;
  readonly nativeSpot?: ZLinkBackendSpot;
  readonly closeWhenReady?: () => void;
  actorDispatch?: ZLinkSpotActorJoinDispatch;
}

export class ZLinkSpotActivation {
  readonly meshName: string;
  readonly spotRid: RoutingId;
  readonly spotType: Type<ZLinkSpot>;
  readonly spot: ZLinkSpot;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly timers: ZLinkSpotTimerRegistry;
  readonly actorHandlers: ZLinkSpotActorHandlerRegistryRuntime;
  readonly handlers: DefaultZLinkSpotHandlerRegistry;
  readonly nativeSpot?: ZLinkBackendSpot;
  actorDispatch?: ZLinkSpotActorJoinDispatch;

  private readonly joinedActors = new Map<string, ZLinkActor>();
  private readonly departedActorIds = new Set<string>();
  private readonly externalActorCount: () => number;
  private readonly closeWhenReady?: () => void;
  private closeRequested = false;
  private drainCloseRequested = false;

  constructor(options: ZLinkSpotActivationOptions) {
    this.meshName = options.meshName;
    this.spotRid = options.spotRid;
    this.spotType = options.spotType;
    this.spot = options.spot;
    this.serial = options.serial;
    this.timers = options.timers;
    this.actorHandlers = options.actorHandlers;
    this.handlers = options.handlers;
    this.externalActorCount = options.externalActorCount ?? (() => 0);
    this.nativeSpot = options.nativeSpot;
    this.closeWhenReady = options.closeWhenReady;
    this.actorDispatch = options.actorDispatch;
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

  requestDrainClose(): void {
    this.closeRequested = true;
    this.drainCloseRequested = true;
    this.notifyCloseReady();
  }

  private notifyCloseReady(): void {
    if (this.drainCloseRequested && this.canClose()) {
      this.closeWhenReady?.();
    }
  }

}
