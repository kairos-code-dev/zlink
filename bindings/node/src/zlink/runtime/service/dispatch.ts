// SPDX-License-Identifier: MPL-2.0

import type { Message, MessageLike } from '../../contracts/messaging';
import type { SubmitResult } from '../../contracts/errors';
import type {
  Claim,
  ClaimReceiveResult,
  ReadyBatch,
  ReadyRecord,
  ReceiveBatch,
  ReceiveRecord
} from '../../contracts/service';
import type { MeshClaimRecvRaw, MeshReceiveRecordRaw } from '../native/binding_service_types';
import { NativeHandle, getNativeHandle } from '../handles/native_handle';
import { requireNative } from '../native/native';
import {
  messageFromNativeBuffer,
  normalizeMessageLikePayload
} from '../buffers/message_conversion';
import { flagsOrZero, maybeActorRefFromRaw, ridOrNull } from './conversions';

/** One materialized inbound record; owns its parts and the private reply token. */
export class RuntimeReceiveRecord implements ReceiveRecord {
  readonly kind: number;
  readonly domain: number;
  readonly sourceNodeRid;
  readonly sourceSpotRid;
  readonly sourceActor;
  readonly operationId;
  readonly operationKind: number;
  readonly channelName: string | null;
  readonly topic: string | null;
  readonly applicationMetadata: Buffer | null;
  readonly terminalResult: number;
  readonly failureErrno: number;
  readonly parts: Message[];
  private readonly _replyToken: Buffer;

  constructor(raw: MeshReceiveRecordRaw) {
    this.kind = raw.kind;
    this.domain = raw.domain;
    this.sourceNodeRid = ridOrNull(raw.sourceNodeRid);
    this.sourceSpotRid = ridOrNull(raw.sourceSpotRid);
    this.sourceActor = maybeActorRefFromRaw(raw.sourceActor);
    this.operationId = raw.operationId;
    this.operationKind = raw.operationKind;
    this.channelName = raw.channelName;
    this.topic = raw.topic;
    this.applicationMetadata = raw.applicationMetadata;
    this.terminalResult = raw.terminalResult;
    this.failureErrno = raw.failureErrno;
    this.parts = raw.parts.map((part) => messageFromNativeBuffer(part));
    this._replyToken = raw.replyToken;
  }

  reply(parts: Message | readonly Message[], flags?: number): SubmitResult {
    return requireNative().meshReply(
      this._replyToken,
      normalizeMessageLikePayload(parts as MessageLike | readonly MessageLike[]),
      flagsOrZero(flags)
    ) as SubmitResult;
  }

  replyActorJoin(joinResult: number, parts: Message | readonly Message[], flags?: number): SubmitResult {
    return requireNative().actorJoinReply(
      this._replyToken,
      joinResult | 0,
      normalizeMessageLikePayload(parts as MessageLike | readonly MessageLike[]),
      flagsOrZero(flags)
    ) as SubmitResult;
  }
}

/** A reusable batch that materializes received messages for one claim. */
export class RuntimeReceiveBatch extends NativeHandle implements ReceiveBatch {
  constructor(native: unknown) {
    super(native);
  }

  reset(): void {
    requireNative().meshReceiveBatchReset(getNativeHandle(this));
  }

  close(): void {
    if (getNativeHandle(this) != null) {
      requireNative().meshReceiveBatchDestroy(getNativeHandle(this));
      this._native = null;
    }
  }
}

/** A claim over one ready record; receive its messages, then release it. */
export class RuntimeClaim extends NativeHandle implements Claim {
  constructor(native: unknown) {
    super(native);
  }

  recvBatch(batch: ReceiveBatch, flags?: number): ClaimReceiveResult {
    const raw: MeshClaimRecvRaw = requireNative().meshClaimRecvBatch(
      getNativeHandle(this),
      getNativeHandle(batch as unknown as NativeHandle),
      flagsOrZero(flags)
    );
    return {
      ok: raw.ok,
      required: raw.required ?? undefined,
      records: raw.records.map((record) => new RuntimeReceiveRecord(record))
    };
  }

  release(): void {
    if (getNativeHandle(this) != null) {
      requireNative().meshClaimRelease(getNativeHandle(this));
      this._native = null;
    }
  }
}

/** A reusable batch that collects ready-index records for one drain pass. */
export class RuntimeReadyBatch extends NativeHandle implements ReadyBatch {
  records: ReadyRecord[] = [];

  constructor(native: unknown) {
    super(native);
  }

  takeClaim(index: number): Claim {
    return new RuntimeClaim(
      requireNative().meshReadyBatchTakeClaim(getNativeHandle(this), index | 0)
    );
  }

  reset(): void {
    requireNative().meshReadyBatchReset(getNativeHandle(this));
    this.records = [];
  }

  close(): void {
    if (getNativeHandle(this) != null) {
      requireNative().meshReadyBatchDestroy(getNativeHandle(this));
      this._native = null;
    }
    this.records = [];
  }
}
