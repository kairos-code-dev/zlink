import { createHash } from 'node:crypto';
import type {
  ZLinkAuthorityCompareExchangeResult,
  ZLinkAuthorityKey,
  ZLinkAuthorityReadResult,
  ZLinkAuthoritySnapshot,
  ZLinkAuthorityStoreVersion
} from '../../contracts/Locations';

const RELOCATION_RETENTION_MS = 24 * 60 * 60 * 1_000;

type Awaitable<T> = T | Promise<T>;

export interface ServiceAuthorityProvider {
  readAuthority(
    key: ZLinkAuthorityKey,
    signal?: AbortSignal
  ): Awaitable<ZLinkAuthorityReadResult>;
  compareExchangeAuthority(
    key: ZLinkAuthorityKey,
    expectedStoreVersion: ZLinkAuthorityStoreVersion,
    mutation: {
      readonly kind: 'put';
      readonly payload: Uint8Array;
      readonly generationTransition: 'preserve';
    },
    signal?: AbortSignal
  ): Awaitable<ZLinkAuthorityCompareExchangeResult>;
}

export interface ServiceRelocationStored {
  readonly reference: string;
  readonly checksumCrc32c: number;
  readonly expiresAtMs: number;
  readonly storeNowMs: number;
}

export interface ServiceRelocationStorePort {
  put(
    payload: Uint8Array,
    retentionMs: number,
    signal?: AbortSignal
  ): Promise<ServiceRelocationStored>;
  get(
    reference: string,
    signal?: AbortSignal
  ): Promise<{ readonly kind: 'found'; readonly payload: Uint8Array } | { readonly kind: 'missing' }>;
  delete(reference: string, signal?: AbortSignal): Promise<'deleted' | 'missing'>;
}

export interface ServiceRelocationParticipant {
  readonly key: string;
  readonly applicationState: Uint8Array;
  readonly acceptedJournal: Uint8Array;
}

export interface ServiceRelocationQueuedMessage {
  readonly sequence: bigint;
  readonly payload: Uint8Array;
}

export interface ServiceRelocationTimer {
  readonly timerId: string;
  readonly startedAtUnixMs: number;
  readonly dueAtUnixMs: number;
  readonly intervalMs: number;
  readonly deliveryIndex: bigint;
  readonly lastScheduledIndex: bigint;
  readonly overrunPolicy: string;
  readonly maxCatchUpTicks: number;
  readonly stopOnUnhandledException: boolean;
  readonly pendingTicks: number;
}

export interface ServiceRelocationEnvelope {
  readonly participants: readonly ServiceRelocationParticipant[];
  readonly queuedMessages: readonly ServiceRelocationQueuedMessage[];
  readonly timers: readonly ServiceRelocationTimer[];
}

export interface ServiceRelocationPublication {
  readonly reference: string;
  readonly checksumCrc32c: number;
  readonly inventoryDigest: string;
}

export interface ServiceRelocationAuthorityCodec {
  publish(
    currentPayload: Uint8Array,
    publication: ServiceRelocationPublication
  ): Uint8Array;
  read(payload: Uint8Array): ServiceRelocationPublication | undefined;
  clear(currentPayload: Uint8Array, expectedReference: string): Uint8Array;
}

export interface ServicePublishedRelocation {
  readonly authority: ZLinkAuthoritySnapshot;
  readonly publication: ServiceRelocationPublication;
}

export class ServiceRelocationDataLostError extends Error {
  constructor(readonly reference: string, message: string) {
    super(message);
    this.name = 'ServiceRelocationDataLostError';
  }
}

/**
 * Stores immutable relocation input first and publishes it with one Location
 * authority CAS. The two providers never need a shared transaction.
 */
export class ServiceDurableRelocationRuntime {
  constructor(
    private readonly authority: ServiceAuthorityProvider,
    private readonly store: ServiceRelocationStorePort,
    private readonly codec: ServiceRelocationAuthorityCodec
  ) {}

  async captureAndPublish(
    key: ZLinkAuthorityKey,
    expected: ZLinkAuthoritySnapshot,
    envelope: ServiceRelocationEnvelope,
    signal?: AbortSignal
  ): Promise<ServicePublishedRelocation> {
    signal?.throwIfAborted();
    const encoded = encodeServiceRelocationEnvelope(envelope);
    const checksumCrc32c = crc32c(encoded);
    const stored = await this.store.put(encoded, RELOCATION_RETENTION_MS, signal);
    if (
      stored.reference.length === 0
      || stored.checksumCrc32c !== checksumCrc32c
      || stored.expiresAtMs <= stored.storeNowMs
    ) {
      await this.store.delete(stored.reference, signal);
      throw new Error('Relocation Store returned an invalid immutable payload receipt.');
    }
    const publication: ServiceRelocationPublication = {
      reference: stored.reference,
      checksumCrc32c,
      inventoryDigest: inventoryDigest(envelope.participants)
    };
    let result: ZLinkAuthorityCompareExchangeResult;
    try {
      result = await this.authority.compareExchangeAuthority(
        key,
        expected.storeVersion,
        {
          kind: 'put',
          generationTransition: 'preserve',
          payload: this.codec.publish(expected.payload, publication)
        },
        signal
      );
    } catch (error) {
      const reconciled = await this.reconcilePublication(
        key,
        expected,
        publication,
        signal
      );
      if (reconciled.kind === 'published') {
        return { authority: reconciled.authority, publication };
      }
      if (reconciled.kind === 'notCommitted') {
        await this.store.delete(stored.reference, signal);
      }
      throw error;
    }
    if (
      result.kind !== 'stored'
      || result.objectGeneration !== expected.objectGeneration
      || result.authorityOwnerGeneration !== expected.authorityOwnerGeneration
    ) {
      await this.store.delete(stored.reference, signal);
      throw new Error('Location authority rejected relocation publication.');
    }
    return { authority: storedSnapshot(result), publication };
  }

  private async reconcilePublication(
    key: ZLinkAuthorityKey,
    expected: ZLinkAuthoritySnapshot,
    publication: ServiceRelocationPublication,
    signal?: AbortSignal
  ): Promise<
    | { readonly kind: 'published'; readonly authority: ZLinkAuthoritySnapshot }
    | { readonly kind: 'notCommitted' }
    | { readonly kind: 'unknown' }
  > {
    let current: ZLinkAuthorityReadResult;
    try {
      current = await this.authority.readAuthority(key, signal);
    } catch {
      return { kind: 'unknown' };
    }
    if (current.kind !== 'snapshot') {
      return { kind: 'unknown' };
    }
    const observed = this.codec.read(current.payload);
    if (samePublication(observed, publication)) {
      return { kind: 'published', authority: current };
    }
    return current.storeVersion.value === expected.storeVersion.value
      ? { kind: 'notCommitted' }
      : { kind: 'unknown' };
  }

  async restore(
    authority: ZLinkAuthoritySnapshot,
    signal?: AbortSignal
  ): Promise<ServiceRelocationEnvelope> {
    signal?.throwIfAborted();
    const publication = this.codec.read(authority.payload);
    if (publication === undefined) {
      throw new Error('Location authority has no published relocation reference.');
    }
    const read = await this.store.get(publication.reference, signal);
    if (read.kind === 'missing') {
      throw new ServiceRelocationDataLostError(
        publication.reference,
        'Location authority references missing relocation data.'
      );
    }
    if (crc32c(read.payload) !== publication.checksumCrc32c) {
      throw new ServiceRelocationDataLostError(
        publication.reference,
        'Published relocation checksum does not match the immutable payload.'
      );
    }
    const envelope = decodeServiceRelocationEnvelope(read.payload);
    if (inventoryDigest(envelope.participants) !== publication.inventoryDigest) {
      throw new ServiceRelocationDataLostError(
        publication.reference,
        'Published relocation inventory does not match Location authority.'
      );
    }
    return envelope;
  }

  async release(
    key: ZLinkAuthorityKey,
    expected: ZLinkAuthoritySnapshot,
    signal?: AbortSignal
  ): Promise<ZLinkAuthoritySnapshot> {
    signal?.throwIfAborted();
    const publication = this.codec.read(expected.payload);
    if (publication === undefined) return expected;
    const result = await this.authority.compareExchangeAuthority(
      key,
      expected.storeVersion,
      {
        kind: 'put',
        generationTransition: 'preserve',
        payload: this.codec.clear(expected.payload, publication.reference)
      },
      signal
    );
    if (result.kind !== 'stored') {
      throw new Error('Location authority rejected relocation release.');
    }
    await this.store.delete(publication.reference, signal);
    return storedSnapshot(result);
  }
}

function storedSnapshot(
  result: Extract<ZLinkAuthorityCompareExchangeResult, { readonly kind: 'stored' }>
): ZLinkAuthoritySnapshot {
  const { kind: _kind, ...snapshot } = result;
  return { kind: 'snapshot', ...snapshot };
}

export function encodeServiceRelocationEnvelope(envelope: ServiceRelocationEnvelope): Buffer {
  const participants = [...envelope.participants]
    .map(participant => ({
      key: requireText(participant.key, 'participant key'),
      applicationState: Buffer.from(participant.applicationState).toString('base64'),
      acceptedJournal: Buffer.from(participant.acceptedJournal).toString('base64')
    }))
    .sort((left, right) => left.key.localeCompare(right.key));
  if (new Set(participants.map(({ key }) => key)).size !== participants.length) {
    throw new TypeError('Relocation participants must have unique keys.');
  }
  const queuedMessages = [...envelope.queuedMessages]
    .map(message => ({
      sequence: positiveBigInt(message.sequence, 'queue sequence').toString(),
      payload: Buffer.from(message.payload).toString('base64')
    }))
    .sort((left, right) => {
      const a = BigInt(left.sequence);
      const b = BigInt(right.sequence);
      return a < b ? -1 : a > b ? 1 : 0;
    });
  if (new Set(queuedMessages.map(({ sequence }) => sequence)).size !== queuedMessages.length) {
    throw new TypeError('Relocation queue sequences must be unique.');
  }
  const timers = [...envelope.timers]
    .map(timer => ({
      timerId: requireText(timer.timerId, 'timer id'),
      startedAtUnixMs: safeInteger(timer.startedAtUnixMs, 'timer start time'),
      dueAtUnixMs: safeInteger(timer.dueAtUnixMs, 'timer due time'),
      intervalMs: positiveInteger(timer.intervalMs, 'timer interval'),
      deliveryIndex: nonNegativeBigInt(timer.deliveryIndex, 'timer delivery index').toString(),
      lastScheduledIndex: nonNegativeBigInt(
        timer.lastScheduledIndex,
        'timer scheduled index'
      ).toString(),
      overrunPolicy: requireText(timer.overrunPolicy, 'timer overrun policy'),
      maxCatchUpTicks: positiveInteger(timer.maxCatchUpTicks, 'timer catch-up limit'),
      stopOnUnhandledException: requireBoolean(
        timer.stopOnUnhandledException,
        'timer stop-on-error flag'
      ),
      pendingTicks: nonNegativeInteger(timer.pendingTicks, 'pending timer ticks')
    }))
    .sort((left, right) => left.timerId.localeCompare(right.timerId));
  if (new Set(timers.map(({ timerId }) => timerId)).size !== timers.length) {
    throw new TypeError('Relocation timer ids must be unique.');
  }
  return Buffer.from(JSON.stringify({
    version: 1,
    inventoryDigest: inventoryDigest(envelope.participants),
    participants,
    queuedMessages,
    timers
  }), 'utf8');
}

export function decodeServiceRelocationEnvelope(payload: Uint8Array): ServiceRelocationEnvelope {
  const parsed = JSON.parse(Buffer.from(payload).toString('utf8')) as {
    readonly version?: unknown;
    readonly inventoryDigest?: unknown;
    readonly participants?: unknown;
    readonly queuedMessages?: unknown;
    readonly timers?: unknown;
  };
  if (
    parsed.version !== 1
    || typeof parsed.inventoryDigest !== 'string'
    || !Array.isArray(parsed.participants)
    || !Array.isArray(parsed.queuedMessages)
    || !Array.isArray(parsed.timers)
  ) {
    throw new TypeError('Invalid relocation envelope.');
  }
  const envelope: ServiceRelocationEnvelope = {
    participants: parsed.participants.map((value: unknown) => {
      const item = record(value, 'participant');
      return {
        key: requireText(item.key, 'participant key'),
        applicationState: base64(item.applicationState, 'application state'),
        acceptedJournal: base64(item.acceptedJournal, 'accepted journal')
      };
    }),
    queuedMessages: parsed.queuedMessages.map((value: unknown) => {
      const item = record(value, 'queued message');
      return {
        sequence: positiveBigInt(item.sequence, 'queue sequence'),
        payload: base64(item.payload, 'queued payload')
      };
    }),
    timers: parsed.timers.map((value: unknown) => {
      const item = record(value, 'timer');
      return {
        timerId: requireText(item.timerId, 'timer id'),
        startedAtUnixMs: safeInteger(item.startedAtUnixMs, 'timer start time'),
        dueAtUnixMs: safeInteger(item.dueAtUnixMs, 'timer due time'),
        intervalMs: positiveInteger(item.intervalMs, 'timer interval'),
        deliveryIndex: nonNegativeBigInt(item.deliveryIndex, 'timer delivery index'),
        lastScheduledIndex: nonNegativeBigInt(
          item.lastScheduledIndex,
          'timer scheduled index'
        ),
        overrunPolicy: requireText(item.overrunPolicy, 'timer overrun policy'),
        maxCatchUpTicks: positiveInteger(item.maxCatchUpTicks, 'timer catch-up limit'),
        stopOnUnhandledException: requireBoolean(
          item.stopOnUnhandledException,
          'timer stop-on-error flag'
        ),
        pendingTicks: nonNegativeInteger(item.pendingTicks, 'pending timer ticks')
      };
    })
  };
  const canonical = encodeServiceRelocationEnvelope(envelope);
  const canonicalParsed = JSON.parse(canonical.toString('utf8')) as { readonly inventoryDigest: string };
  if (canonicalParsed.inventoryDigest !== parsed.inventoryDigest) {
    throw new TypeError('Relocation inventory digest mismatch.');
  }
  return envelope;
}

export function inventoryDigest(participants: readonly ServiceRelocationParticipant[]): string {
  const keys = participants.map(({ key }) => requireText(key, 'participant key')).sort();
  if (new Set(keys).size !== keys.length) {
    throw new TypeError('Relocation participants must have unique keys.');
  }
  return createHash('sha256').update(keys.join('\0'), 'utf8').digest('hex');
}

export function crc32c(payload: Uint8Array): number {
  let crc = 0xffff_ffff;
  for (const byte of payload) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) {
      crc = (crc >>> 1) ^ ((crc & 1) === 0 ? 0 : 0x82f6_3b78);
    }
  }
  return (crc ^ 0xffff_ffff) >>> 0;
}

function samePublication(
  left: ServiceRelocationPublication | undefined,
  right: ServiceRelocationPublication
): boolean {
  return left?.reference === right.reference
    && left.checksumCrc32c === right.checksumCrc32c
    && left.inventoryDigest === right.inventoryDigest;
}

function record(value: unknown, label: string): Record<string, unknown> {
  if (value === null || typeof value !== 'object' || Array.isArray(value)) {
    throw new TypeError(`Invalid relocation ${label}.`);
  }
  return value as Record<string, unknown>;
}

function requireText(value: unknown, label: string): string {
  if (typeof value !== 'string' || value.length === 0 || value.includes('\0')) {
    throw new TypeError(`${label} must be non-empty text without NUL.`);
  }
  return value;
}

function base64(value: unknown, label: string): Buffer {
  if (typeof value !== 'string' || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/u.test(value)) {
    throw new TypeError(`${label} must be canonical base64.`);
  }
  const bytes = Buffer.from(value, 'base64');
  if (bytes.toString('base64') !== value) throw new TypeError(`${label} must be canonical base64.`);
  return bytes;
}

function positiveBigInt(value: unknown, label: string): bigint {
  let parsed: bigint;
  try {
    parsed = typeof value === 'bigint' ? value : BigInt(requireText(value, label));
  } catch {
    throw new TypeError(`${label} must be a positive integer.`);
  }
  if (parsed <= 0n) throw new TypeError(`${label} must be a positive integer.`);
  return parsed;
}

function nonNegativeBigInt(value: unknown, label: string): bigint {
  let parsed: bigint;
  try {
    parsed = typeof value === 'bigint' ? value : BigInt(requireText(value, label));
  } catch {
    throw new TypeError(`${label} must be a non-negative integer.`);
  }
  if (parsed < 0n) throw new TypeError(`${label} must be a non-negative integer.`);
  return parsed;
}

function safeInteger(value: unknown, label: string): number {
  if (typeof value !== 'number' || !Number.isSafeInteger(value)) {
    throw new TypeError(`${label} must be a safe integer.`);
  }
  return value;
}

function positiveInteger(value: unknown, label: string): number {
  const parsed = safeInteger(value, label);
  if (parsed <= 0) throw new TypeError(`${label} must be positive.`);
  return parsed;
}

function nonNegativeInteger(value: unknown, label: string): number {
  const parsed = safeInteger(value, label);
  if (parsed < 0) throw new TypeError(`${label} must not be negative.`);
  return parsed;
}

function requireBoolean(value: unknown, label: string): boolean {
  if (typeof value !== 'boolean') throw new TypeError(`${label} must be boolean.`);
  return value;
}
