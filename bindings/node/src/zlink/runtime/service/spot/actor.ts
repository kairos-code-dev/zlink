// SPDX-License-Identifier: MPL-2.0

import { NativeHandle } from '../../handles/native_handle';
import { requireNative } from '../../native/native';
import { closeCall, configCall, recvNativeError } from '../../errors/native_errors';
import { RecvFlags } from '../../../contracts/sockets/socket_constants';
import type { ActorJoinOperation, ActorLeaveOperation, ActorPart, ActorRef, SendOperation } from '../../../contracts/service';
import { RuntimeSendOperation } from '../../sockets/socket_operation_builders';
import { Spot } from './spot';
import { actorPartFromRaw, actorRefToRaw, type ActorPartRaw } from './actor_models';
import { invokeActorJoin, invokeActorLeave, invokeActorSendBoundSession } from './actor_invokers';
import { RuntimeActorJoinOperation, RuntimeActorLeaveOperation } from './actor_operations';

export class Actor extends NativeHandle {
  private static readonly CREATE_TOKEN = Symbol('Actor.create');
  private _ref: ActorRef | null;
  private constructor(token: symbol, nodeHandle: unknown, ref: ActorRef) {
    if (token !== Actor.CREATE_TOKEN) {
      throw new TypeError('Actor values are created by SpotNode.createActor()');
    }
    super(nodeHandle);
    this._ref = ref;
  }
  /** @internal */
  static create(nodeHandle: unknown, ref: ActorRef): Actor {
    return new Actor(Actor.CREATE_TOKEN, nodeHandle, ref);
  }
  ref(): ActorRef {
    if (!this._ref) {
      throw new Error('actor is closed');
    }
    return this._ref;
  }
  get actorRef(): ActorRef {
    return this.ref();
  }
  join(spot: Spot): ActorJoinOperation {
    if (!(spot instanceof Spot)) {
      throw new TypeError('spot must be a Spot');
    }
    return new RuntimeActorJoinOperation((parts, callback, flags, timeoutMs) =>
      invokeActorJoin(
        this._native,
        this.ref(),
        spot.ownerNodeRoutingId(),
        spot.routingId,
        parts,
        callback,
        flags,
        timeoutMs,
      ),
    );
  }
  leave(spot: Spot): ActorLeaveOperation {
    if (!(spot instanceof Spot)) {
      throw new TypeError('spot must be a Spot');
    }
    const actorRef = this.ref();
    const spotRid = spot.routingId;
    return new RuntimeActorLeaveOperation((callback, timeoutMs) =>
      invokeActorLeave(this._native, actorRef, spotRid, callback, timeoutMs),
    );
  }
  recvPart(flags: RecvFlags = RecvFlags.None): ActorPart | null {
    let raw;
    try {
      raw = requireNative().spotNodeActorRecvPart(
        this._native,
        actorRefToRaw(this.ref()),
        flags | 0
      ) as ActorPartRaw | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'actor part recv failed');
    }
    return raw ? actorPartFromRaw(raw) : null;
  }
  sendBoundSession(): SendOperation {
    const node = this._native;
    const ref = this.ref();
    return new RuntimeSendOperation((parts, flags) => invokeActorSendBoundSession(node, ref, parts, flags));
  }
  closeBoundSession(timeoutMs = 0): void {
    const actorRaw = actorRefToRaw(this.ref());
    configCall('actor bound session close failed', () => {
      requireNative().spotNodeActorCloseBoundSession(
        this._native,
        actorRaw,
        timeoutMs | 0
      );
    });
  }
  close(timeoutMs = 0): void {
    if (!this._native || !this._ref) {
      return;
    }
    const actorRaw = actorRefToRaw(this._ref);
    closeCall('actor close failed', () => {
      requireNative().spotNodeActorDestroy(this._native, actorRaw, timeoutMs | 0);
    });
    this._ref = null;
    this._native = null;
  }
}
