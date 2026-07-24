import type {
  ActorRef,
  RoutingId,
  SpotId,
  Type,
  ZLinkActor,
  ZLinkActorContext,
  ZLinkActorJoinCompletion,
  ZLinkActorJoinEntrySpotCall,
  ZLinkActorJoinSpotCall,
  ZLinkActorHandlerRegistry,
  ZLinkBoundSession,
  ZLinkSpot
} from '../../contracts';
import {
  ZLinkEncodedPayload,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkMessage
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { Message as BindingMessage } from '@zlink-systems/zlink';
import { type ZLinkMessageSerializer } from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import {
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
import { ZLinkSpotActorHandlerRegistryRuntime } from './spot-actor-dispatch';
import type { ZLinkActorManagerOptions } from './actor-runtime-contracts';
import {
  ZLINK_ACTOR_LIFECYCLE_SNAPSHOT,
  type ZLinkActorLifecycleSnapshotSource
} from './actor-lifecycle-snapshot';
import { randomBytes } from 'node:crypto';
import { deferActorJoin } from './actor-join-deferred-scope';

const pendingDeferredJoinStates = new WeakSet<ZLinkActorRuntimeState>();

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

  get actorId(): string {
    return this.state.actorId;
  }

  get objectGeneration(): bigint {
    return this.actorRef?.generation ?? 1n;
  }

  get spotId(): SpotId | undefined {
    return this.state.spotId;
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

  joinSpot(spotId: SpotId, request?: unknown): ZLinkActorJoinSpotCall {
    return new DefaultZLinkActorJoinSpotCall(
      this.state,
      this.requireActor(),
      this.requireJoinCoordinator(),
      spotId,
      request,
      this.messageSerializers
    );
  }

  joinEntrySpot(request?: unknown): ZLinkActorJoinEntrySpotCall {
    return new DefaultZLinkActorJoinEntrySpotCall(
      this.state,
      this.requireActor(),
      this.requireJoinCoordinator(),
      request,
      this.messageSerializers
    );
  }

  async joinEntrySpotForRuntime(
    nodeRid: RoutingId | undefined,
    request: unknown,
    signal?: AbortSignal
  ): Promise<boolean> {
    const requestMessage = encodeJoinRequest(request, this.messageSerializers);
    try {
      return (await this.requireJoinCoordinator().joinEntrySpot(
        this.requireActor(),
        this.state,
        nodeRid,
        requestMessage,
        undefined,
        signal
      )).accepted;
    } finally {
      requestMessage.close();
    }
  }

  async leaveSpot(signal?: AbortSignal): Promise<void> {
    const spotId = this.state.spotId;
    if (spotId === undefined) {
      throw new ZLinkConfigurationException('Actor has not joined a user SPOT.');
    }
    if (this.leaveSpotRuntime === undefined) {
      throw new ZLinkConfigurationException('Actor Spot lifecycle runtime is not started.');
    }
    await this.leaveSpotRuntime(this.meshName, spotId, this.requireActor(), signal);
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
  private timeoutMs = 5_000;
  private deferred = false;

  constructor(
    private readonly state: ZLinkActorRuntimeState,
    private readonly actor: ZLinkActor,
    private readonly coordinator: ZLinkActorJoinCoordinator,
    private readonly spotId: SpotId,
    private readonly request: unknown,
    private readonly messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer> | undefined
  ) {
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = validateJoinTimeout(timeoutMs);
    return this;
  }

  defer(): void {
    if (this.deferred) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.AlreadySubmitted,
        'Actor join call was already deferred.'
      );
    }
    this.deferred = true;
    const deadline = Date.now() + this.timeoutMs;
    const sourceNodeRid = this.state.nativeActorRef?.nodeRid;
    const requestMessage = encodeJoinRequest(this.request, this.messageSerializers);
    try {
      claimDeferredJoin(this.state);
    } catch (error) {
      requestMessage.close();
      throw error;
    }
    const operationId = createJoinOperationId();
    deferActorJoin({
      requestBytes: requestMessage.data().byteLength,
      discard: () => {
        pendingDeferredJoinStates.delete(this.state);
        requestMessage.close();
      },
      execute: async () => {
        let result: import('./actor-runtime-contracts').ZLinkActorJoinRuntimeResult<Message>;
        try {
          result = await this.coordinator.joinSpot(
            this.actor,
            this.state,
            this.spotId,
            requestMessage,
            remainingJoinTimeout(deadline),
            undefined
          );
        } catch (error) {
          await notifyJoinFailure(this.actor, operationId, error);
          return;
        } finally {
          pendingDeferredJoinStates.delete(this.state);
          requestMessage.close();
        }
        if (
          sourceNodeRid === undefined
          || result.actor === undefined
          || String(sourceNodeRid) === String(result.actor.nodeRid)
        ) {
          await notifyJoinCompletion(this.actor, operationId, result);
        } else {
          result.reply?.close();
        }
      }
    });
  }
}

class DefaultZLinkActorJoinEntrySpotCall implements ZLinkActorJoinEntrySpotCall {
  private timeoutMs = 5_000;
  private deferred = false;

  constructor(
    private readonly state: ZLinkActorRuntimeState,
    private readonly actor: ZLinkActor,
    private readonly coordinator: ZLinkActorJoinCoordinator,
    private readonly request: unknown,
    private readonly messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer> | undefined
  ) {
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = validateJoinTimeout(timeoutMs);
    return this;
  }

  defer(): void {
    if (this.deferred) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.AlreadySubmitted,
        'Actor join call was already deferred.'
      );
    }
    this.deferred = true;
    const deadline = Date.now() + this.timeoutMs;
    const sourceNodeRid = this.state.nativeActorRef?.nodeRid;
    const requestMessage = encodeJoinRequest(this.request, this.messageSerializers);
    try {
      claimDeferredJoin(this.state);
    } catch (error) {
      requestMessage.close();
      throw error;
    }
    const operationId = createJoinOperationId();
    deferActorJoin({
      requestBytes: requestMessage.data().byteLength,
      discard: () => {
        pendingDeferredJoinStates.delete(this.state);
        requestMessage.close();
      },
      execute: async () => {
        let result: import('./actor-runtime-contracts').ZLinkActorJoinRuntimeResult<Message>;
        try {
          result = await this.coordinator.joinEntrySpot(
            this.actor,
            this.state,
            undefined,
            requestMessage,
            remainingJoinTimeout(deadline),
            undefined
          );
        } catch (error) {
          await notifyJoinFailure(this.actor, operationId, error);
          return;
        } finally {
          pendingDeferredJoinStates.delete(this.state);
          requestMessage.close();
        }
        if (
          sourceNodeRid === undefined
          || result.actor === undefined
          || String(sourceNodeRid) === String(result.actor.nodeRid)
        ) {
          await notifyJoinCompletion(this.actor, operationId, result);
        } else {
          result.reply?.close();
        }
      }
    });
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

function encodeJoinRequest(
  request: unknown,
  serializers: ReadonlyMap<string, ZLinkMessageSerializer> | undefined
): Message {
  return request === undefined
    ? BindingMessage.from(Buffer.alloc(0))
    : encodeFrameworkPayloadMessage(request, serializers);
}

function validateJoinTimeout(timeoutMs: number): number {
  const rounded = Math.ceil(timeoutMs);
  if (!Number.isFinite(timeoutMs) || rounded < 1 || rounded > 2_147_483_647) {
    throw new ZLinkConfigurationException(
      'Actor join timeout must be a finite value from 1 through 2147483647 milliseconds.'
    );
  }
  return rounded;
}

function claimDeferredJoin(state: ZLinkActorRuntimeState): void {
  if (pendingDeferredJoinStates.has(state) || state.isMoving) {
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorMoving,
      `Actor '${state.actorId}' already has a pending membership transition.`,
      true
    );
  }
  pendingDeferredJoinStates.add(state);
}

function remainingJoinTimeout(deadline: number): number {
  const remaining = deadline - Date.now();
  if (remaining <= 0) {
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.DeadlineExceeded,
      'Deferred Actor join deadline elapsed before activation.',
      true
    );
  }
  return remaining;
}

function createJoinOperationId(): { readonly high: bigint; readonly low: bigint } {
  const bytes = randomBytes(16);
  return {
    high: bytes.readBigUInt64BE(0),
    low: bytes.readBigUInt64BE(8)
  };
}

async function notifyJoinCompletion(
  actor: ZLinkActor,
  operationId: { readonly high: bigint; readonly low: bigint },
  result: import('./actor-runtime-contracts').ZLinkActorJoinRuntimeResult<Message>
): Promise<void> {
  const reply = result.reply === undefined
    ? undefined
    : ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(result.reply.data()));
  result.reply?.close();
  const completion: ZLinkActorJoinCompletion = result.accepted
    ? {
        status: 'accepted',
        operationId,
        actor: result.actor!,
        reply
      }
    : {
        status: 'rejected',
        operationId,
        reply
      };
  await actor.onJoinCompleted?.(completion);
}

async function notifyJoinFailure(
  actor: ZLinkActor,
  operationId: { readonly high: bigint; readonly low: bigint },
  error: unknown
): Promise<void> {
  const frameworkError = error instanceof ZLinkFrameworkException
    ? error
    : new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestFailed,
        'Deferred actor join failed.',
        true,
        error
      );
  await actor.onJoinCompleted?.({
    status: 'failed',
    operationId,
    kind: frameworkError.kind,
    isRetriable: frameworkError.isRetriable
  });
}
