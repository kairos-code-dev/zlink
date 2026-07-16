import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkActorContext,
  ZLinkSpot
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkSpotKind
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import { routingIdsEqual } from '../routing-id';

export interface ZLinkRemoteBoundSessionTarget {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotRid: RoutingId;
  readonly sessionNodeRid?: RoutingId;
  readonly sessionRid?: RoutingId;
}

export interface ZLinkRemoteActorPacketTarget {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotRid: RoutingId;
  readonly spotKind?: ZLinkSpotKind;
}

export interface ZLinkActorCreationOperation {
  readonly task: Promise<ZLinkActor>;
  readonly created: boolean;
}

export class ZLinkActorRuntimeState {
  private creationTask: Promise<ZLinkActor> | undefined;
  private configured = false;
  private context: ZLinkActorContext | undefined;
  private actorTypeValue: string | undefined;
  private actorValue: ZLinkActor | undefined;
  private spotValue: ZLinkSpot | undefined;
  private spotRidValue: RoutingId | undefined;
  private nativeActorRefValue: ZLinkBackendActorRef | undefined;
  private entryNodeRidValue: RoutingId | undefined;
  private remoteBoundSessionTargetValue: ZLinkRemoteBoundSessionTarget | undefined;
  private remoteActorPacketTargetValue: ZLinkRemoteActorPacketTarget | undefined;
  private createRequestPayloadValue: Buffer | undefined;
  private ownsLocationValue = false;
  private locationGenerationValue: bigint | undefined;
  private movingValue = false;
  private destroyTask: Promise<void> | undefined;

  constructor(readonly actorId: string) {}

  get actorType(): string | undefined {
    return this.actorTypeValue;
  }

  get actor(): ZLinkActor | undefined {
    return this.actorValue;
  }

  get spot(): ZLinkSpot | undefined {
    return this.spotValue;
  }

  get spotRid(): RoutingId | undefined {
    return this.spotRidValue;
  }

  get nativeActorRef(): ZLinkBackendActorRef | undefined {
    return this.nativeActorRefValue;
  }

  get entryNodeRid(): RoutingId | undefined {
    return this.entryNodeRidValue;
  }

  get remoteBoundSessionTarget(): ZLinkRemoteBoundSessionTarget | undefined {
    return this.remoteBoundSessionTargetValue;
  }

  get remoteActorPacketTarget(): ZLinkRemoteActorPacketTarget | undefined {
    return this.remoteActorPacketTargetValue;
  }

  get createRequestPayload(): Buffer | undefined {
    return this.createRequestPayloadValue;
  }

  get isJoined(): boolean {
    return this.spotRidValue !== undefined;
  }

  get ownsLocation(): boolean {
    return this.ownsLocationValue;
  }

  get locationGeneration(): bigint | undefined {
    return this.locationGenerationValue;
  }

  get isMoving(): boolean {
    return this.movingValue;
  }

  get hasActorOrCreation(): boolean {
    return this.actorValue !== undefined || this.creationTask !== undefined;
  }

  beginMove(): void {
    if (this.movingValue) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${this.actorId}' is already moving.`
      );
    }
    this.movingValue = true;
  }

  endMove(): void {
    this.movingValue = false;
  }

  markLocationOwned(): void {
    this.ownsLocationValue = true;
  }

  markLocationReleased(): void {
    this.ownsLocationValue = false;
  }

  setLocationGeneration(generation: bigint): void {
    this.locationGenerationValue = generation;
  }

  getOrStartDestroy(
    entryNodeRid: RoutingId,
    destroy: (actorRef: ZLinkBackendActorRef | undefined) => Promise<void>
  ): Promise<void> {
    if (this.destroyTask !== undefined) {
      return this.destroyTask;
    }
    const actorRef = this.nativeActorRefValue;
    if (actorRef !== undefined && !routingIdsEqual(toFrameworkRoutingId(actorRef.nodeRid), entryNodeRid)) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${this.actorId}' is not owned by this Entry Spot.`
      );
    }

    const task = Promise.resolve().then(() => destroy(actorRef));
    this.destroyTask = task;
    return task;
  }

  markNativeActorDestroyed(actorRef: ZLinkBackendActorRef): void {
    if (this.nativeActorRefValue === actorRef) {
      this.nativeActorRefValue = undefined;
    }
  }

  clearFailedDestroy(task: Promise<void>): void {
    if (this.destroyTask === task) {
      this.destroyTask = undefined;
    }
    this.movingValue = false;
  }

  ensureContext(createContext: () => ZLinkActorContext): ZLinkActorContext {
    this.context ??= createContext();
    return this.context;
  }

  getOrStartCreation(
    actorType: string,
    failIfExists: boolean,
    createActor: () => Promise<ZLinkActor>
  ): ZLinkActorCreationOperation {
    if (this.actorTypeValue !== undefined && this.actorTypeValue !== actorType) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorTypeMismatch,
        `Actor '${this.actorId}' already uses actor type '${this.actorTypeValue}', not '${actorType}'.`
      );
    }

    if (this.actorValue !== undefined) {
      if (failIfExists) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorAlreadyExists,
          `Actor '${this.actorId}' already exists.`
        );
      }
      return { task: Promise.resolve(this.actorValue), created: false };
    }

    if (this.creationTask !== undefined) {
      if (failIfExists) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorAlreadyExists,
          `Actor '${this.actorId}' is already being created.`
        );
      }
      return { task: this.creationTask, created: false };
    }

    this.actorTypeValue = actorType;
    this.creationTask = createActor();
    return { task: this.creationTask, created: true };
  }

  clearFailedCreation(task: Promise<ZLinkActor>): void {
    if (this.creationTask === task && this.actorValue === undefined) {
      this.creationTask = undefined;
      this.actorTypeValue = undefined;
      this.createRequestPayloadValue = undefined;
      this.configured = false;
    }
  }

  bindActor(actor: ZLinkActor, context: ZLinkActorContext): void {
    if (actor.actorId !== this.actorId) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorCreateFailed,
        `Actor state id '${this.actorId}' does not match actor id '${actor.actorId}'.`
      );
    }
    if (actor.context !== context) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorCreateFailed,
        `Actor '${this.actorId}' must expose the context provided by its factory.`
      );
    }

    this.actorValue = actor;
    this.creationTask = undefined;
    if (!this.configured) {
      actor.configure?.();
      this.configured = true;
    }
  }

  ensureNativeActorRef(node: ZLinkBackendSpotNode, request?: Message): ZLinkBackendActorRef {
    this.nativeActorRefValue ??= node.actorLookup(this.actorId) ?? node.createActor(this.actorId, request);
    this.entryNodeRidValue ??= toFrameworkRoutingId(this.nativeActorRefValue.nodeRid);
    return this.nativeActorRefValue;
  }

  setNativeActorRef(actorRef: ZLinkBackendActorRef): void {
    this.nativeActorRefValue = actorRef;
    this.entryNodeRidValue ??= toFrameworkRoutingId(actorRef.nodeRid);
  }

  setEntryNodeRid(entryNodeRid: RoutingId): void {
    this.entryNodeRidValue = entryNodeRid;
  }

  setRemoteBoundSessionTarget(target: ZLinkRemoteBoundSessionTarget | undefined): void {
    const current = this.remoteBoundSessionTargetValue;
    this.remoteBoundSessionTargetValue = target === undefined ||
      target.sessionNodeRid !== undefined ||
      current?.sessionNodeRid === undefined ||
      current.routerChannelId !== target.routerChannelId ||
      !routingIdsEqual(current.targetNodeRid, target.targetNodeRid) ||
      !routingIdsEqual(current.spotRid, target.spotRid)
      ? target
      : {
          ...target,
          sessionNodeRid: current.sessionNodeRid,
          sessionRid: current.sessionRid
        };
  }

  setRemoteActorPacketTarget(target: ZLinkRemoteActorPacketTarget | undefined): void {
    this.remoteActorPacketTargetValue = target;
  }

  setCreateRequestPayload(payload: Buffer | Uint8Array): void {
    this.createRequestPayloadValue = Buffer.from(payload);
  }

  setJoinedSpot(spotRid: RoutingId, spot?: ZLinkSpot): void {
    this.spotRidValue = spotRid;
    this.spotValue = spot;
  }

  clearJoinedSpot(): void {
    this.spotRidValue = undefined;
    this.spotValue = undefined;
  }

  clearAfterDestroy(): void {
    this.creationTask = undefined;
    this.configured = false;
    this.context = undefined;
    this.actorTypeValue = undefined;
    this.actorValue = undefined;
    this.spotValue = undefined;
    this.spotRidValue = undefined;
    this.nativeActorRefValue = undefined;
    this.entryNodeRidValue = undefined;
    this.remoteBoundSessionTargetValue = undefined;
    this.remoteActorPacketTargetValue = undefined;
    this.createRequestPayloadValue = undefined;
    this.ownsLocationValue = false;
    this.locationGenerationValue = undefined;
    this.destroyTask = undefined;
  }

  prepareForRemoteReentry(): void {
    if (this.remoteActorPacketTargetValue === undefined) return;
    this.creationTask = undefined;
    this.configured = false;
    this.context = undefined;
    this.actorTypeValue = undefined;
    this.actorValue = undefined;
    this.spotValue = undefined;
    this.spotRidValue = undefined;
    this.nativeActorRefValue = undefined;
    this.remoteBoundSessionTargetValue = undefined;
    this.remoteActorPacketTargetValue = undefined;
    this.createRequestPayloadValue = undefined;
    this.ownsLocationValue = false;
    this.locationGenerationValue = undefined;
    this.movingValue = false;
    this.destroyTask = undefined;
  }
}

export function toFrameworkRoutingId(routingId: ZLinkBackendActorRef['nodeRid']): RoutingId {
  return String(routingId);
}

export function toFrameworkActorRef(actor: ZLinkBackendActorRef): ActorRef {
  return {
    nodeRid: toFrameworkRoutingId(actor.nodeRid),
    actorId: actor.actorId,
    generation: actor.generation
  };
}
