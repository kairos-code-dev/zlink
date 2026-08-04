import { crc32c } from './service-relocation-runtime';
import {
  validateApplicationPayloadFrame
} from './service-wire-m6a-codec';
import type { ServiceInstanceActivationTarget } from './service-stateful-wire-codec';
import { validateServiceMetadataFrame } from './service-metadata-codec';

const MAGIC = Buffer.from([0x5a, 0x4c, 0x49, 0x41]);
const VERSION = 1;
const FLAGS = 0;
const MAX_ENCODED_BYTES = 1024 * 1024;
const FATAL_UTF8 = new TextDecoder('utf-8', { fatal: true });

export interface ServiceInstanceActivationRecoveryEnvelope {
  readonly target: ServiceInstanceActivationTarget;
  readonly targetMeshName: string;
  readonly sourceNodeRid: string;
  readonly sourceNodeGeneration: bigint;
  readonly sourceSpotId?: string;
  readonly operationKind: 'send' | 'request';
  readonly operation: { readonly high: bigint; readonly low: bigint };
  readonly replyRouteId?: bigint;
  readonly deadlineUnixMs: bigint;
  readonly metadataFrame?: Uint8Array;
  readonly applicationPayloadFrame: Uint8Array;
}

export function encodeInstanceActivationRecoveryEnvelope(
  value: ServiceInstanceActivationRecoveryEnvelope
): Buffer {
  requireOperation(value);
  validateApplicationPayloadFrame(value.applicationPayloadFrame);
  const applicationPayload = Buffer.from(value.applicationPayloadFrame);
  const body = concat(
    text8(value.target.targetSpotId, 'targetSpotId'),
    text8(value.target.stableType, 'stableType'),
    text8(value.targetMeshName, 'targetMeshName'),
    text8(value.target.targetNodeRid, 'targetNodeRid'),
    u64(value.target.targetNodeGeneration, 'targetNodeGeneration'),
    text8(value.target.descriptorVersion, 'targetDescriptorVersion'),
    text8(value.sourceNodeRid, 'sourceNodeRid'),
    u64(value.sourceNodeGeneration, 'sourceNodeGeneration'),
    optionalText8(value.sourceSpotId, 'sourceSpotId'),
    Buffer.of(value.operationKind === 'send' ? 1 : 2),
    u64Any(value.operation.high, 'operation.high'),
    u64Any(value.operation.low, 'operation.low'),
    ...(value.operationKind === 'request'
      ? [u64(value.replyRouteId!, 'replyRouteId')]
      : []),
    u64(value.deadlineUnixMs, 'deadlineUnixMs'),
    ...(value.metadataFrame === undefined
      ? [Buffer.of(0)]
      : [Buffer.of(1), validateServiceMetadataFrame(value.metadataFrame)]),
    applicationPayload
  );
  const envelope = concat(
    MAGIC,
    Buffer.of(VERSION),
    u16(FLAGS),
    u32(body.byteLength),
    body
  );
  const encoded = concat(envelope, u32(crc32c(envelope)));
  if (encoded.byteLength > MAX_ENCODED_BYTES) {
    throw new RangeError('Instance activation recovery envelope exceeds 1 MiB.');
  }
  return encoded;
}

export function decodeInstanceActivationRecoveryEnvelope(
  encoded: Uint8Array
): ServiceInstanceActivationRecoveryEnvelope {
  if (encoded.byteLength > MAX_ENCODED_BYTES) {
    throw new RangeError('Instance activation recovery envelope exceeds 1 MiB.');
  }
  const reader = new RecoveryReader(encoded);
  reader.expect(MAGIC);
  if (reader.u8() !== VERSION || reader.u16() !== FLAGS) {
    throw new TypeError('Instance activation recovery envelope version or flags are invalid.');
  }
  const body = reader.takeReader(reader.u32());
  const checksumOffset = reader.offset;
  const checksum = reader.u32();
  if (!reader.done || crc32c(reader.bytes.subarray(0, checksumOffset)) !== checksum) {
    throw new TypeError('Instance activation recovery envelope checksum is invalid.');
  }
  const targetSpotId = body.text8();
  const stableType = body.text8();
  const targetMeshName = body.text8();
  const targetNodeRid = body.text8();
  const targetNodeGeneration = body.nonZeroU64();
  const descriptorVersion = body.text8();
  const sourceNodeRid = body.text8();
  const sourceNodeGeneration = body.nonZeroU64();
  const sourceSpotId = body.optionalText8();
  const operationDiscriminator = body.u8();
  if (operationDiscriminator !== 1 && operationDiscriminator !== 2) {
    throw new TypeError('Instance activation recovery operation kind is invalid.');
  }
  const operation = { high: body.u64(), low: body.u64() };
  if (operation.high === 0n && operation.low === 0n) {
    throw new TypeError('Instance activation recovery operation identity is zero.');
  }
  const operationKind = operationDiscriminator === 1 ? 'send' : 'request';
  const replyRouteId = operationKind === 'request' ? body.nonZeroU64() : undefined;
  const deadlineUnixMs = body.nonZeroU64();
  const hasMetadata = body.u8();
  if (hasMetadata !== 0 && hasMetadata !== 1) {
    throw new TypeError('Instance activation recovery metadata flag is invalid.');
  }
  const metadataFrame = hasMetadata === 1
    ? body.metadataFrame()
    : undefined;
  const applicationPayloadFrame = body.takeRemaining();
  validateApplicationPayloadFrame(applicationPayloadFrame);
  if (!body.done) {
    throw new TypeError('Instance activation recovery envelope has trailing bytes.');
  }
  return {
    target: {
      targetSpotId,
      stableType,
      targetNodeRid,
      targetNodeGeneration,
      descriptorVersion
    },
    targetMeshName,
    sourceNodeRid,
    sourceNodeGeneration,
    ...(sourceSpotId === undefined ? {} : { sourceSpotId }),
    operationKind,
    operation,
    ...(replyRouteId === undefined ? {} : { replyRouteId }),
    deadlineUnixMs,
    ...(metadataFrame === undefined ? {} : { metadataFrame }),
    applicationPayloadFrame
  };
}

function requireOperation(value: ServiceInstanceActivationRecoveryEnvelope): void {
  if (value.operation.high === 0n && value.operation.low === 0n) {
    throw new TypeError('Instance activation recovery operation identity is zero.');
  }
  if (
    value.operationKind === 'request'
      ? value.replyRouteId === undefined || value.replyRouteId <= 0n
      : value.replyRouteId !== undefined
  ) {
    throw new TypeError('Instance activation recovery reply route does not match the operation.');
  }
}

function text8(value: string, name: string): Buffer {
  const bytes = Buffer.from(value, 'utf8');
  if (bytes.byteLength < 1 || bytes.byteLength > 255 || bytes.includes(0)) {
    throw new RangeError(`${name} must contain 1..255 UTF-8 bytes without NUL.`);
  }
  return concat(Buffer.of(bytes.byteLength), bytes);
}

function optionalText8(value: string | undefined, name: string): Buffer {
  return value === undefined ? Buffer.of(0) : concat(Buffer.of(1), text8(value, name));
}

function u16(value: number): Buffer {
  const result = Buffer.allocUnsafe(2);
  result.writeUInt16BE(value);
  return result;
}

function u32(value: number): Buffer {
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffff_ffff) {
    throw new RangeError('u32 value is out of range.');
  }
  const result = Buffer.allocUnsafe(4);
  result.writeUInt32BE(value);
  return result;
}

function u64(value: bigint, name: string): Buffer {
  if (value <= 0n) throw new RangeError(`${name} must be positive.`);
  return u64Any(value, name);
}

function u64Any(value: bigint, name: string): Buffer {
  if (value < 0n || value > 0xffff_ffff_ffff_ffffn) {
    throw new RangeError(`${name} is out of range.`);
  }
  const result = Buffer.allocUnsafe(8);
  result.writeBigUInt64BE(value);
  return result;
}

function concat(...values: readonly Uint8Array[]): Buffer {
  return Buffer.concat(values.map(value => Buffer.from(value)));
}

class RecoveryReader {
  readonly bytes: Uint8Array;
  offset = 0;

  constructor(value: Uint8Array) {
    this.bytes = value;
  }

  get done(): boolean {
    return this.offset === this.bytes.byteLength;
  }

  expect(expected: Uint8Array): void {
    const actual = this.take(expected.byteLength);
    if (!Buffer.from(actual).equals(Buffer.from(expected))) {
      throw new TypeError('Instance activation recovery envelope magic is invalid.');
    }
  }

  u8(): number {
    return this.take(1)[0]!;
  }

  u16(): number {
    return Buffer.from(this.take(2)).readUInt16BE();
  }

  u32(): number {
    return Buffer.from(this.take(4)).readUInt32BE();
  }

  u64(): bigint {
    return Buffer.from(this.take(8)).readBigUInt64BE();
  }

  nonZeroU64(): bigint {
    const value = this.u64();
    if (value === 0n) throw new TypeError('Instance activation recovery field is zero.');
    return value;
  }

  text8(): string {
    const bytes = this.take(this.u8());
    if (bytes.byteLength === 0 || bytes.includes(0)) {
      throw new TypeError('Instance activation recovery text is invalid.');
    }
    return FATAL_UTF8.decode(bytes);
  }

  optionalText8(): string | undefined {
    const present = this.u8();
    if (present === 0) return undefined;
    if (present !== 1) throw new TypeError('Instance activation recovery optional flag is invalid.');
    return this.text8();
  }

  metadataFrame(): Buffer {
    const start = this.offset;
    if (this.u8() !== 1) {
      throw new TypeError('Application metadata frame version is invalid.');
    }
    const count = this.u8();
    for (let index = 0; index < count; index += 1) {
      this.take(this.u8());
      this.take(this.u16());
    }
    return validateServiceMetadataFrame(this.bytes.subarray(start, this.offset));
  }

  takeRemaining(): Uint8Array {
    return this.take(this.bytes.byteLength - this.offset);
  }

  takeReader(length: number): RecoveryReader {
    return new RecoveryReader(this.take(length));
  }

  private take(length: number): Uint8Array {
    if (length < 0 || this.offset + length > this.bytes.byteLength) {
      throw new TypeError('Instance activation recovery envelope is truncated.');
    }
    const value = this.bytes.subarray(this.offset, this.offset + length);
    this.offset += length;
    return value;
  }
}
