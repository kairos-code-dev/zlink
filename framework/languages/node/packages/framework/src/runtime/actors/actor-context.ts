import type {
  ActorRef,
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkActorContext,
  ZLinkActorJoinEntrySpotCall,
  ZLinkActorJoinResult,
  ZLinkActorJoinSpotCall,
  ZLinkActorHandlerRegistry,
  ZLinkBoundSession,
  ZLinkSpot
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { Message as BindingMessage } from '@zlink-systems/zlink';
import { type ZLinkMessageSerializer } from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import {
  decodeFrameworkPayloadMessage,
  encodeFrameworkPayloadMessage
} from '../messaging/payload-codec';
import {
  ZLinkActorRuntimeState,
  toFrameworkActorRef
} from './actor-runtime-state';
import type {
  ZLinkActorBoundSessionFactory,
  ZLinkActorJoinCoordinator
} from './actor-runtime-contracts';
import { captureZLinkSpotSerialTurn, type ZLinkSpotSerialTurn } from '../execution';
import { ZLinkSpotActorHandlerRegistryRuntime } from './spot-actor-dispatch';
import type { ZLinkActorManagerOptions } from './actor-runtime-contracts';
import {
  ZLINK_ACTOR_LIFECYCLE_SNAPSHOT,
  type ZLinkActorLifecycleSnapshotSource
} from './actor-lifecycle-snapshot';

export class DefaultZLinkActorContext implements ZLinkActorContext {
  readonly boundSession: ZLinkBoundSession;
  readonly handlers: ZLinkActorHandlerRegistry = new ZLinkSpotActorHandlerRegistryRuntime();

  constructor(
    private readonly state: ZLinkActorRuntimeState,
    private readonly joinCoordinator: ZLinkActorJoinCoordinator | undefined,
    boundSessionFactory: ZLinkActorBoundSessionFactory | undefined,
    private readonly messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer> | undefined,
    private readonly meshNameProvider: ZLinkActorManagerOptions['actorMeshNameProvider'],
    private readonly leaveSpotRuntime: ZLinkActorManagerOptions['actorLeaveSpot']
  ) {
    this.boundSession = boundSessionFactory?.(state.actorId) ?? new UnboundZLinkSession();
  }

  get meshName(): string {
    const actorType = this.state.actorType;
    const meshName = actorType === undefined ? undefined : this.meshNameProvider?.(actorType);
    if (meshName === undefined) {
      throw new ZLinkConfigurationException(
        `Actor '${this.state.actorId}' does not belong to a registered RouteMesh.`
      );
    }
    return meshName;
  }

  get spotRid(): RoutingId | undefined {
    return this.state.spotRid;
  }

  get isJoined(): boolean {
    return this.state.isJoined;
  }

  get actorRef(): ActorRef | undefined {
    const actorRef = this.state.nativeActorRef;
    return actorRef === undefined
      ? undefined
      : toFrameworkActorRef(actorRef);
  }

  [ZLINK_ACTOR_LIFECYCLE_SNAPSHOT](): ZLinkActorLifecycleSnapshotSource {
    const actorRef = this.actorRef;
    const actorType = this.state.actorType;
    if (actorRef === undefined || actorType === undefined) {
      throw new ZLinkConfigurationException(
        `Actor '${this.state.actorId}' lifecycle identity is not initialized.`
      );
    }
    return {
      actorRef,
      actorType,
      membershipEpoch: this.state.spotMembershipEpoch
    };
  }

  getSpot<TSpot extends ZLinkSpot>(spotType?: Type<TSpot>): ZLinkSpot | TSpot {
    const spot = this.state.spot;
    if (spot === undefined) {
      throw new ZLinkConfigurationException('Actor has not joined a SPOT.');
    }
    if (spotType !== undefined && !(spot instanceof spotType)) {
      throw new ZLinkConfigurationException('Actor joined SPOT has a different spot type.');
    }
    return spot;
  }

  joinSpot(spotRid: RoutingId, request?: unknown): ZLinkActorJoinSpotCall {
    return new DefaultZLinkActorJoinSpotCall(
      this.state,
      this.requireActor(),
      this.requireJoinCoordinator(),
      spotRid,
      request,
      this.messageSerializers
    );
  }

  joinEntrySpot(nodeRid: RoutingId, request: unknown): ZLinkActorJoinEntrySpotCall {
    return new DefaultZLinkActorJoinEntrySpotCall(
      this.state,
      this.requireActor(),
      this.requireJoinCoordinator(),
      nodeRid,
      request,
      this.messageSerializers
    );
  }

  async leaveSpot(signal?: AbortSignal): Promise<void> {
    const spotRid = this.state.spotRid;
    if (spotRid === undefined) {
      throw new ZLinkConfigurationException('Actor has not joined a user SPOT.');
    }
    if (this.leaveSpotRuntime === undefined) {
      throw new ZLinkConfigurationException('Actor Spot lifecycle runtime is not started.');
    }
    await this.leaveSpotRuntime(this.meshName, spotRid, this.requireActor(), signal);
  }

  private requireActor(): ZLinkActor {
    if (this.state.actor === undefined) {
      throw new ZLinkConfigurationException('Actor context is not bound to an actor.');
    }
    return this.state.actor;
  }

  private requireJoinCoordinator(): ZLinkActorJoinCoordinator {
    if (this.joinCoordinator === undefined) {
      throw new ZLinkConfigurationException('Actor join runtime is not started.');
    }
    return this.joinCoordinator;
  }
}

class DefaultZLinkActorJoinSpotCall implements ZLinkActorJoinSpotCall {
  private timeoutMs: number | undefined;
  private readonly turn: ZLinkSpotSerialTurn | undefined = captureZLinkSpotSerialTurn();

  constructor(
    private readonly state: ZLinkActorRuntimeState,
    private readonly actor: ZLinkActor,
    private readonly coordinator: ZLinkActorJoinCoordinator,
    private readonly spotRid: RoutingId,
    private readonly request: unknown,
    private readonly messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer> | undefined
  ) {
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  async submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>> {
    return await this.execute<TReply>(signal);
  }

  async yield<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>> {
    const pending = this.execute<TReply>(signal);
    return this.turn === undefined ? pending : this.turn.yieldPromise(pending);
  }

  private async execute<TReply>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>> {
    const requestMessage = this.request === undefined
      ? BindingMessage.from(Buffer.alloc(0))
      : encodeFrameworkPayloadMessage(this.request, this.messageSerializers);
    try {
      const result = await this.coordinator.joinSpot(
        this.actor,
        this.state,
        this.spotRid,
        requestMessage,
        this.timeoutMs,
        signal
      );
      const reply = decodeJoinReply<TReply>(result.reply, this.messageSerializers) as TReply;
      return result.accepted
        ? { status: 'accepted', actor: result.actor!, reply }
        : { status: 'rejected', rejection: reply };
    } finally {
      requestMessage.close();
    }
  }

}

class DefaultZLinkActorJoinEntrySpotCall implements ZLinkActorJoinEntrySpotCall {
  private timeoutMs: number | undefined;
  private readonly turn: ZLinkSpotSerialTurn | undefined = captureZLinkSpotSerialTurn();

  constructor(
    private readonly state: ZLinkActorRuntimeState,
    private readonly actor: ZLinkActor,
    private readonly coordinator: ZLinkActorJoinCoordinator,
    private readonly nodeRid: RoutingId,
    private readonly request: unknown,
    private readonly messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer> | undefined
  ) {
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  async submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>> {
    return await this.execute<TReply>(signal);
  }

  async yield<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>> {
    const pending = this.execute<TReply>(signal);
    return this.turn === undefined ? pending : this.turn.yieldPromise(pending);
  }

  private async execute<TReply>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>> {
    const requestMessage = this.request === undefined
      ? BindingMessage.from(Buffer.alloc(0))
      : encodeFrameworkPayloadMessage(this.request, this.messageSerializers);
    try {
      const result = await this.coordinator.joinEntrySpot(
        this.actor,
        this.state,
        this.nodeRid,
        requestMessage,
        this.timeoutMs,
        signal
      );
      const reply = decodeJoinReply<TReply>(result.reply, this.messageSerializers) as TReply;
      return result.accepted
        ? { status: 'accepted', actor: result.actor!, reply }
        : { status: 'rejected', rejection: reply };
    } finally {
      requestMessage.close();
    }
  }

}

class UnboundZLinkSession implements ZLinkBoundSession {
  send(): never {
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorSessionNotBound,
      'Actor session is not bound.',
      true
    );
  }

  async disconnect(): Promise<void> {
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorSessionNotBound,
      'Actor session is not bound.',
      true
    );
  }
}

function decodeJoinReply<TReply>(
  reply: Message | undefined,
  serializers: ReadonlyMap<string, ZLinkMessageSerializer> | undefined
): TReply | undefined {
  if (reply === undefined) return undefined;
  try {
    return decodeFrameworkPayloadMessage<TReply>(reply, serializers);
  } finally {
    reply.close();
  }
}
