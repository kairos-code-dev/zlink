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
  ZLinkBackendMeshNode
} from '../backend/contracts';
import { routingIdsEqual } from '../routing-id';
import { lookupNativeActorRef } from './actor-native-lookup';

export interface ZLinkRemoteBoundSessionTarget {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotId: RoutingId;
  readonly sessionNodeRid?: RoutingId;
  readonly sessionRid?: RoutingId;
  readonly bindingGeneration?: bigint;
}

export interface ZLinkRemoteActorPacketTarget {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotId: RoutingId;
  readonly spotKind?: ZLinkSpotKind;
}

export interface ZLinkActorCreationOperation {
  readonly task: Promise<ZLinkActorCreationAttemptResult>;
  readonly created: boolean;
}

export type ZLinkActorCreationAttemptResult =
  | {
      readonly status: 'created';
      readonly actor: ZLinkActor;
      readonly reply?: unknown;
    }
  | {
      readonly status: 'rejected';
      readonly reply?: unknown;
    };

export class ZLinkActorRuntimeState {
  private creationTask: Promise<ZLinkActorCreationAttemptResult> | undefined;
  private configured = false;
  private context: ZLinkActorContext | undefined;
  private actorTypeValue: string | undefined;
  private meshNameValue: string | undefined;
  private actorValue: ZLinkActor | undefined;
  private spotValue: ZLinkSpot | undefined;
  private spotIdValue: RoutingId | undefined;
  private spotMembershipEpochValue = 0n;
  private nativeActorRefValue: ZLinkBackendActorRef | undefined;
  private boundSessionBindingGenerationValue = 0n;
  private entryNodeRidValue: RoutingId | undefined;
  private remoteBoundSessionTargetValue: ZLinkRemoteBoundSessionTarget | undefined;
  private boundSessionTransferTargetValue: ZLinkRemoteBoundSessionTarget | undefined;
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

  // The manager resolves RouteMesh membership once when it registers the
  // actor, so identity reads never re-enter the application provider.
  get meshName(): string | undefined {
    return this.meshNameValue;
  }

  rememberMeshName(meshName: string): void {
    this.meshNameValue = meshName;
  }

  get actor(): ZLinkActor | undefined {
    return this.actorValue;
  }

  get spot(): ZLinkSpot | undefined {
    return this.spotValue;
  }

  get spotId(): RoutingId | undefined {
    return this.spotIdValue;
  }

  get spotMembershipEpoch(): bigint {
    return this.spotMembershipEpochValue;
  }

  get nativeActorRef(): ZLinkBackendActorRef | undefined {
    return this.nativeActorRefValue;
  }

  get boundSessionBindingGeneration(): bigint {
    return this.boundSessionBindingGenerationValue;
  }

  get entryNodeRid(): RoutingId | undefined {
    return this.entryNodeRidValue;
  }

  get remoteBoundSessionTarget(): ZLinkRemoteBoundSessionTarget | undefined {
    return this.remoteBoundSessionTargetValue;
  }

  get boundSessionTransferTarget(): ZLinkRemoteBoundSessionTarget | undefined {
    return this.boundSessionTransferTargetValue;
  }

  get remoteActorPacketTarget(): ZLinkRemoteActorPacketTarget | undefined {
    return this.remoteActorPacketTargetValue;
  }

  get createRequestPayload(): Buffer | undefined {
    return this.createRequestPayloadValue;
  }

  get isJoined(): boolean {
    return this.spotIdValue !== undefined;
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
    createActor: () => Promise<ZLinkActorCreationAttemptResult>
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
      return {
        task: Promise.resolve({ status: 'created', actor: this.actorValue }),
        created: false
      };
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

  clearFailedCreation(task: Promise<ZLinkActorCreationAttemptResult>): boolean {
    if (this.creationTask === task && this.actorValue === undefined) {
      this.creationTask = undefined;
      this.actorTypeValue = undefined;
      this.createRequestPayloadValue = undefined;
      this.configured = false;
      this.nativeActorRefValue = undefined;
      this.entryNodeRidValue = undefined;
      return true;
    }
    return false;
  }

  bindActor(actor: ZLinkActor, context: ZLinkActorContext): void {
    if (actor.context.actorId !== this.actorId) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorCreateFailed,
        `Actor state id '${this.actorId}' does not match actor id '${actor.context.actorId}'.`
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

  ensureNativeActorRef(node: ZLinkBackendMeshNode, request?: Message): ZLinkBackendActorRef {
    if (this.nativeActorRefValue === undefined) {
      const existing = lookupNativeActorRef(node, this.actorId);
      const native = existing ?? node.createActor(
        this.actorId,
        request === undefined ? undefined : Buffer.from(request.data())
      );
      this.nativeActorRefValue = {
        nodeRid: native.nodeRid as ZLinkBackendActorRef['nodeRid'],
        actorId: native.actorId,
        generation: native.generation
      };
    }
    this.entryNodeRidValue ??= toFrameworkRoutingId(this.nativeActorRefValue.nodeRid);
    return this.nativeActorRefValue;
  }

  setNativeActorRef(actorRef: ZLinkBackendActorRef): void {
    this.nativeActorRefValue = actorRef;
    this.entryNodeRidValue ??= toFrameworkRoutingId(actorRef.nodeRid);
  }

  setBoundSessionBindingGeneration(generation: bigint): void {
    if (generation > 0n) {
      this.boundSessionBindingGenerationValue = generation;
      if (this.remoteBoundSessionTargetValue !== undefined) {
        this.remoteBoundSessionTargetValue = {
          ...this.remoteBoundSessionTargetValue,
          bindingGeneration: generation
        };
      }
      if (this.boundSessionTransferTargetValue !== undefined) {
        this.boundSessionTransferTargetValue = {
          ...this.boundSessionTransferTargetValue,
          bindingGeneration: generation
        };
      }
    }
  }

  setEntryNodeRid(entryNodeRid: RoutingId): void {
    this.entryNodeRidValue = entryNodeRid;
  }

  setRemoteBoundSessionTarget(target: ZLinkRemoteBoundSessionTarget | undefined): void {
    const current = this.remoteBoundSessionTargetValue;
    const merged = target === undefined ||
      target.sessionNodeRid !== undefined ||
      current?.sessionNodeRid === undefined ||
      current.routerChannelId !== target.routerChannelId ||
      !routingIdsEqual(current.targetNodeRid, target.targetNodeRid) ||
      current.spotId !== target.spotId
      ? target
      : {
          ...target,
          sessionNodeRid: current.sessionNodeRid,
          sessionRid: current.sessionRid
        };
    const bindingGeneration = merged?.bindingGeneration
      ?? current?.bindingGeneration
      ?? (
        this.boundSessionBindingGenerationValue > 0n
          ? this.boundSessionBindingGenerationValue
          : undefined
      );
    this.remoteBoundSessionTargetValue = merged === undefined
      ? undefined
      : bindingGeneration === undefined
        ? merged
        : { ...merged, bindingGeneration };
  }

  setBoundSessionTransferTarget(target: ZLinkRemoteBoundSessionTarget | undefined): void {
    const bindingGeneration = target?.bindingGeneration
      ?? (
        this.boundSessionBindingGenerationValue > 0n
          ? this.boundSessionBindingGenerationValue
          : undefined
      );
    this.boundSessionTransferTargetValue = target === undefined
      ? undefined
      : bindingGeneration === undefined
        ? target
        : { ...target, bindingGeneration };
  }

  setRemoteActorPacketTarget(target: ZLinkRemoteActorPacketTarget | undefined): void {
    this.remoteActorPacketTargetValue = target;
  }

  setCreateRequestPayload(payload: Buffer | Uint8Array): void {
    this.createRequestPayloadValue = Buffer.from(payload);
  }

  setJoinedSpot(spotId: RoutingId, spot?: ZLinkSpot, membershipEpoch = 0n): void {
    this.spotIdValue = spotId;
    this.spotValue = spot;
    this.spotMembershipEpochValue = membershipEpoch;
  }

  clearJoinedSpot(): void {
    this.spotIdValue = undefined;
    this.spotValue = undefined;
    this.spotMembershipEpochValue = 0n;
  }

  clearAfterDestroy(): void {
    this.creationTask = undefined;
    this.configured = false;
    this.context = undefined;
    this.actorTypeValue = undefined;
    this.actorValue = undefined;
    this.spotValue = undefined;
    this.spotIdValue = undefined;
    this.spotMembershipEpochValue = 0n;
    this.nativeActorRefValue = undefined;
    this.boundSessionBindingGenerationValue = 0n;
    this.entryNodeRidValue = undefined;
    this.remoteBoundSessionTargetValue = undefined;
    this.boundSessionTransferTargetValue = undefined;
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
    this.spotIdValue = undefined;
    this.nativeActorRefValue = undefined;
    this.boundSessionBindingGenerationValue = 0n;
    this.remoteBoundSessionTargetValue = undefined;
    this.boundSessionTransferTargetValue = undefined;
    this.remoteActorPacketTargetValue = undefined;
    this.createRequestPayloadValue = undefined;
    this.ownsLocationValue = false;
    this.locationGenerationValue = undefined;
    this.movingValue = false;
    this.destroyTask = undefined;
  }
}

export function toFrameworkRoutingId(routingId: unknown): RoutingId {
  // Runtime routing identities are opaque binary values. Preserve binding
  // RoutingId instances so a later native call cannot reinterpret their
  // hexadecimal display string as different literal bytes.
  return routingId as unknown as RoutingId;
}

export function toFrameworkActorRef(actor: ZLinkBackendActorRef): ActorRef {
  return {
    nodeRid: toFrameworkRoutingId(actor.nodeRid),
    actorId: actor.actorId,
    generation: actor.generation
  };
}
