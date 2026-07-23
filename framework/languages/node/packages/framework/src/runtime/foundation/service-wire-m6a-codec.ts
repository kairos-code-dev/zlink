import {
  validateDescriptor,
  type ServiceNodeDescriptor,
  type ServiceNodeState,
  type ServiceObjectRole
} from './service-topology-registry';

const PREFIX_SIZE = 5;
const MAX_U32 = 0xffff_ffff;
const MAX_U64 = 0xffff_ffff_ffff_ffffn;
export const M6A_SERVICE_WIRE_MAGIC = [0x5a, 0x4d] as const;
export const M6A_SERVICE_WIRE_MAJOR = 1;
export const M6A_SERVICE_WIRE_REQUIRED_CAPABILITY = 'framework-service-v11';
export const M6aServiceWireCommand = Object.freeze({
  hello: 1,
  admit: 2,
  reject: 3,
  update: 4,
  livenessProbe: 5,
  livenessAck: 6,
  nodeSend: 16,
  nodeRequest: 17,
  channelSend: 18,
  channelRequest: 19,
  reply: 20
});

export interface ServiceApplicationPayload {
  readonly packetName: string;
  readonly contentType: string;
  readonly payload: Uint8Array;
}

export interface ServiceWireHeader {
  readonly command: number;
  readonly flags: number;
}

export interface ServiceReplyHeader {
  readonly correlation: bigint;
  readonly terminalResult: number;
  readonly failureCode: number;
}

export class ServiceWireProtocolError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ServiceWireProtocolError';
  }
}

export function encodeNodeSendHeader(): Buffer {
  return prefix(M6aServiceWireCommand.nodeSend);
}

export function encodeNodeRequestHeader(correlation: bigint): Buffer {
  return withU64(prefix(M6aServiceWireCommand.nodeRequest), correlation, 'correlation');
}

export function decodeNodeRequestHeader(frame: Uint8Array): bigint {
  const header = decodeHeader(frame);
  if (header.command !== M6aServiceWireCommand.nodeRequest || header.flags !== 0 || frame.byteLength !== 13) {
    fail('Invalid nodeRequest header.');
  }
  return readNonZeroU64(asBuffer(frame), PREFIX_SIZE, 'correlation');
}

export function encodeChannelSendHeader(channelName: string): Buffer {
  return concat(prefix(M6aServiceWireCommand.channelSend), encodeText8(channelName, 'channelName'));
}

export function decodeChannelSendHeader(frame: Uint8Array): string {
  const header = decodeHeader(frame);
  if (header.command !== M6aServiceWireCommand.channelSend || header.flags !== 0) {
    fail('Invalid channelSend header.');
  }
  const reader = new Reader(frame, PREFIX_SIZE);
  const channelName = reader.text8('channelName');
  reader.end();
  return channelName;
}

export function encodeChannelRequestHeader(correlation: bigint, channelName: string): Buffer {
  return concat(
    withU64(prefix(M6aServiceWireCommand.channelRequest), correlation, 'correlation'),
    encodeText8(channelName, 'channelName')
  );
}

export function decodeChannelRequestHeader(
  frame: Uint8Array
): { readonly correlation: bigint; readonly channelName: string } {
  const header = decodeHeader(frame);
  if (header.command !== M6aServiceWireCommand.channelRequest || header.flags !== 0) {
    fail('Invalid channelRequest header.');
  }
  const reader = new Reader(frame, PREFIX_SIZE);
  const correlation = reader.nonZeroU64('correlation');
  const channelName = reader.text8('channelName');
  reader.end();
  return { correlation, channelName };
}

export function encodeReplyHeader(
  correlation: bigint,
  terminalResult = 0,
  failureCode = 0
): Buffer {
  validateReplyFields(correlation, terminalResult, failureCode);
  const result = Buffer.alloc(PREFIX_SIZE + 16);
  prefix(M6aServiceWireCommand.reply).copy(result);
  result.writeBigUInt64BE(correlation, PREFIX_SIZE);
  result.writeUInt32BE(terminalResult, PREFIX_SIZE + 8);
  result.writeUInt32BE(failureCode, PREFIX_SIZE + 12);
  return result;
}

export function decodeReplyHeader(frame: Uint8Array): ServiceReplyHeader {
  const header = decodeHeader(frame);
  if (header.command !== M6aServiceWireCommand.reply || header.flags !== 0 || frame.byteLength !== 21) {
    fail('Invalid reply header.');
  }
  const bytes = asBuffer(frame);
  const result = {
    correlation: bytes.readBigUInt64BE(PREFIX_SIZE),
    terminalResult: bytes.readUInt32BE(PREFIX_SIZE + 8),
    failureCode: bytes.readUInt32BE(PREFIX_SIZE + 12)
  };
  validateReplyFields(result.correlation, result.terminalResult, result.failureCode);
  return result;
}

export function encodeApplicationPayload(payload: ServiceApplicationPayload): Buffer {
  const packetName = encodeText8(payload.packetName, 'packetName');
  const contentType = encodeText8(payload.contentType, 'contentType');
  if (payload.payload.byteLength > MAX_U32) fail('Application payload exceeds u32.');
  const body = Buffer.alloc(packetName.byteLength + contentType.byteLength + 4 + payload.payload.byteLength);
  let offset = 0;
  offset += packetName.copy(body, offset);
  offset += contentType.copy(body, offset);
  body.writeUInt32BE(payload.payload.byteLength, offset);
  offset += 4;
  Buffer.from(payload.payload).copy(body, offset);
  const result = Buffer.alloc(5 + body.byteLength);
  result[0] = 1;
  result.writeUInt32BE(body.byteLength, 1);
  body.copy(result, 5);
  return result;
}

export function decodeApplicationPayload(frame: Uint8Array): ServiceApplicationPayload {
  const reader = new Reader(frame);
  if (reader.u8('version') !== 1) fail('Invalid application payload version.');
  const bodyLength = reader.u32('bodyLength');
  if (bodyLength !== reader.remaining) fail('Application payload body length mismatch.');
  const packetName = reader.text8('packetName');
  const contentType = reader.text8('contentType');
  const payloadLength = reader.u32('payloadLength');
  if (payloadLength !== reader.remaining) fail('Application payload length mismatch.');
  const payload = reader.bytes(payloadLength, 'payload');
  reader.end();
  return { packetName, contentType, payload };
}

export function encodeRouteMeshAdmission(
  command: number,
  descriptor: ServiceNodeDescriptor
): Buffer {
  validateAdmissionCommand(command);
  validateDescriptor(descriptor);
  const routeParts: Buffer[] = [
    encodeText8(descriptor.meshName, 'meshName'),
    encodeText8(descriptor.securityIdentity, 'securityIdentity'),
    encodeU32(descriptor.effectiveMaxMessageBytes),
    encodeU64(descriptor.lifecycleGeneration),
    encodeU64(descriptor.descriptorRevision),
    encodeText16(descriptor.advertisedEndpoint, 'advertisedEndpoint'),
    encodeU16(descriptor.channels.length)
  ];
  for (const channel of descriptor.channels) {
    routeParts.push(encodeText8(channel.name, 'channelName'), encodeU32(channel.weight));
  }
  const extension = concat(
    tlv(1, Buffer.of(stateToWire(descriptor.state))),
    tlv(2, encodeU64(descriptor.applicationVersion)),
    tlv(6, concat(
      encodeU16(descriptor.protocolCapabilities.length),
      ...descriptor.protocolCapabilities.map(value => encodeText8(value, 'protocolCapability'))
    )),
    tlv(7, Buffer.of(roleToWire(descriptor.objectRole))),
    tlv(8, encodeU32(descriptor.placementWeight)),
    tlv(9, encodeU32(descriptor.activeCapacityLimit)),
    tlv(10, encodeU32(descriptor.pendingCapacityLimit)),
    tlv(11, encodeU32(descriptor.activeCapacityUsed)),
    tlv(12, encodeU32(descriptor.pendingCapacityUsed))
  );
  const route = concat(...routeParts, encodeU32(extension.byteLength), extension);
  return concat(prefix(command), Buffer.of(1), encodeU32(route.byteLength), route);
}

export function decodeRouteMeshAdmission(
  frame: Uint8Array,
  expectedCommand: number,
  sourceRoutingId: string
): ServiceNodeDescriptor {
  validateAdmissionCommand(expectedCommand);
  const header = decodeHeader(frame);
  if (header.command !== expectedCommand || header.flags !== 0) fail('Unexpected admission header.');
  const reader = new Reader(frame, PREFIX_SIZE);
  if (reader.u8('topologyKind') !== 1) fail('Admission is not RouteMesh topology.');
  const routeLength = reader.u32('routeLength');
  if (routeLength !== reader.remaining) fail('RouteMesh admission length mismatch.');
  const meshName = reader.text8('meshName');
  const securityIdentity = reader.text8('securityIdentity');
  const effectiveMaxMessageBytes = reader.u32('effectiveMaxMessageBytes');
  const lifecycleGeneration = reader.nonZeroU64('lifecycleGeneration');
  const descriptorRevision = reader.nonZeroU64('descriptorRevision');
  const advertisedEndpoint = reader.text16('advertisedEndpoint');
  const channelCount = reader.u16('channelCount');
  const channels: Array<{ name: string; weight: number }> = [];
  for (let index = 0; index < channelCount; index++) {
    channels.push({ name: reader.text8('channelName'), weight: reader.u32('channelWeight') });
  }
  const extensionLength = reader.u32('extensionLength');
  const extensionEnd = reader.offset + extensionLength;
  if (extensionEnd !== frame.byteLength) fail('Descriptor extension length mismatch.');

  let state: ServiceNodeState | undefined;
  let applicationVersion: bigint | undefined;
  let protocolCapabilities: string[] | undefined;
  let objectRole: ServiceObjectRole | undefined;
  const capacities = new Map<number, number>();
  let previousId = 0;
  while (reader.offset < extensionEnd) {
    const id = reader.u8('extensionId');
    const length = reader.u32('extensionFieldLength');
    if (id <= previousId || length > extensionEnd - reader.offset) fail('Invalid descriptor TLV.');
    previousId = id;
    const value = new Reader(reader.bytes(length, 'extensionValue'));
    switch (id) {
      case 1:
        state = stateFromWire(value.u8('state'));
        break;
      case 2:
        applicationVersion = value.u64('applicationVersion');
        break;
      case 6: {
        const count = value.u16('capabilityCount');
        protocolCapabilities = [];
        for (let index = 0; index < count; index++) {
          protocolCapabilities.push(value.text8('protocolCapability'));
        }
        break;
      }
      case 7:
        objectRole = roleFromWire(value.u8('objectRole'));
        break;
      case 8:
      case 9:
      case 10:
      case 11:
      case 12:
        capacities.set(id, value.u32('capacity'));
        break;
      default:
        value.skipRemaining();
        break;
    }
    value.end();
  }
  reader.end();
  if (
    state === undefined
    || applicationVersion === undefined
    || protocolCapabilities === undefined
    || objectRole === undefined
    || ![8, 9, 10, 11, 12].every(id => capacities.has(id))
  ) {
    fail('Descriptor extension omits a required field.');
  }
  const descriptor: ServiceNodeDescriptor = {
    meshName,
    nodeRoutingId: sourceRoutingId,
    lifecycleGeneration,
    descriptorRevision,
    advertisedEndpoint,
    channels,
    state,
    securityIdentity,
    effectiveMaxMessageBytes,
    applicationVersion,
    protocolCapabilities,
    objectRole,
    placementWeight: capacities.get(8)!,
    activeCapacityLimit: capacities.get(9)!,
    pendingCapacityLimit: capacities.get(10)!,
    activeCapacityUsed: capacities.get(11)!,
    pendingCapacityUsed: capacities.get(12)!
  };
  validateDescriptor(descriptor);
  return descriptor;
}

export function encodeReject(reason: number): Buffer {
  if (!Number.isInteger(reason) || reason < 1 || reason > 12) fail('Invalid reject reason.');
  return concat(prefix(M6aServiceWireCommand.reject), encodeU32(reason));
}

export function decodeReject(frame: Uint8Array): number {
  const header = decodeHeader(frame);
  if (header.command !== M6aServiceWireCommand.reject || header.flags !== 0 || frame.byteLength !== 9) {
    fail('Invalid reject record.');
  }
  const reason = asBuffer(frame).readUInt32BE(PREFIX_SIZE);
  if (reason < 1 || reason > 12) fail('Invalid reject reason.');
  return reason;
}

export function decodeHeader(frame: Uint8Array): ServiceWireHeader {
  if (frame.byteLength < PREFIX_SIZE) fail('Truncated service wire prefix.');
  if (frame[0] !== M6A_SERVICE_WIRE_MAGIC[0] || frame[1] !== M6A_SERVICE_WIRE_MAGIC[1]) {
    fail('Invalid service wire magic.');
  }
  if (frame[2] !== M6A_SERVICE_WIRE_MAJOR) fail('Unsupported service wire major.');
  return { command: frame[3]!, flags: frame[4]! };
}

export function requiredServiceCapability(): string {
  return M6A_SERVICE_WIRE_REQUIRED_CAPABILITY;
}

function prefix(command: number): Buffer {
  return Buffer.from([
    M6A_SERVICE_WIRE_MAGIC[0],
    M6A_SERVICE_WIRE_MAGIC[1],
    M6A_SERVICE_WIRE_MAJOR,
    command,
    0
  ]);
}

function withU64(head: Buffer, value: bigint, field: string): Buffer {
  if (value <= 0n || value > MAX_U64) fail(`${field} must be a non-zero u64.`);
  return concat(head, encodeU64(value));
}

function encodeU16(value: number): Buffer {
  if (!Number.isInteger(value) || value < 0 || value > 0xffff) fail('Value exceeds u16.');
  const result = Buffer.alloc(2);
  result.writeUInt16BE(value);
  return result;
}

function encodeU32(value: number): Buffer {
  if (!Number.isInteger(value) || value < 0 || value > MAX_U32) fail('Value exceeds u32.');
  const result = Buffer.alloc(4);
  result.writeUInt32BE(value);
  return result;
}

function encodeU64(value: bigint): Buffer {
  if (value < 0n || value > MAX_U64) fail('Value exceeds u64.');
  const result = Buffer.alloc(8);
  result.writeBigUInt64BE(value);
  return result;
}

function encodeText8(value: string, field: string): Buffer {
  const bytes = Buffer.from(value, 'utf8');
  if (bytes.byteLength === 0 || bytes.byteLength > 0xff || value.includes('\0')) {
    fail(`${field} must be bounded non-empty UTF-8 without NUL.`);
  }
  return concat(Buffer.of(bytes.byteLength), bytes);
}

function encodeText16(value: string, field: string): Buffer {
  const bytes = Buffer.from(value, 'utf8');
  if (bytes.byteLength === 0 || bytes.byteLength > 4096 || value.includes('\0')) {
    fail(`${field} must be bounded non-empty UTF-8 without NUL.`);
  }
  return concat(encodeU16(bytes.byteLength), bytes);
}

function tlv(id: number, value: Buffer): Buffer {
  return concat(Buffer.of(id), encodeU32(value.byteLength), value);
}

function concat(...parts: readonly Uint8Array[]): Buffer {
  return Buffer.concat(parts.map(part => Buffer.from(part)));
}

function readNonZeroU64(bytes: Buffer, offset: number, field: string): bigint {
  const result = bytes.readBigUInt64BE(offset);
  if (result === 0n) fail(`${field} must be non-zero.`);
  return result;
}

function asBuffer(value: Uint8Array): Buffer {
  return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
}

function validateAdmissionCommand(command: number): void {
  if (
    command !== M6aServiceWireCommand.hello
    && command !== M6aServiceWireCommand.admit
    && command !== M6aServiceWireCommand.update
  ) {
    fail('Command is not an admission record.');
  }
}

function validateReplyFields(correlation: bigint, terminal: number, failure: number): void {
  const typedFailure = terminal === 102 || (terminal >= 104 && terminal <= 107);
  const validFailure = (failure >= 0 && failure <= 22) || failure === 35;
  if (
    correlation <= 0n
    || correlation > MAX_U64
    || (terminal !== 0 && (terminal < 101 || terminal > 113))
    || !validFailure
    || (terminal === 0 && failure !== 0)
    || (typedFailure && failure === 0)
    || (!typedFailure && failure !== 0)
  ) {
    fail('Invalid reply terminal fields.');
  }
}

function stateToWire(value: ServiceNodeState): number {
  return ['preparing', 'serving', 'retiring', 'draining', 'stopped', 'error'].indexOf(value) + 1;
}

function stateFromWire(value: number): ServiceNodeState {
  if (!Number.isInteger(value) || value < 1 || value > 6) fail('Invalid runtime state.');
  return ['preparing', 'serving', 'retiring', 'draining', 'stopped', 'error'][value - 1] as ServiceNodeState;
}

function roleToWire(value: ServiceObjectRole): number {
  return ['none', 'client', 'server'].indexOf(value);
}

function roleFromWire(value: number): ServiceObjectRole {
  if (!Number.isInteger(value) || value < 0 || value > 2) fail('Invalid object role.');
  return ['none', 'client', 'server'][value] as ServiceObjectRole;
}

class Reader {
  readonly buffer: Buffer;
  offset: number;

  constructor(frame: Uint8Array, offset = 0) {
    this.buffer = asBuffer(frame);
    this.offset = offset;
  }

  get remaining(): number {
    return this.buffer.byteLength - this.offset;
  }

  u8(field: string): number {
    this.require(1, field);
    return this.buffer[this.offset++]!;
  }

  u16(field: string): number {
    this.require(2, field);
    const result = this.buffer.readUInt16BE(this.offset);
    this.offset += 2;
    return result;
  }

  u32(field: string): number {
    this.require(4, field);
    const result = this.buffer.readUInt32BE(this.offset);
    this.offset += 4;
    return result;
  }

  u64(field: string): bigint {
    this.require(8, field);
    const result = this.buffer.readBigUInt64BE(this.offset);
    this.offset += 8;
    return result;
  }

  nonZeroU64(field: string): bigint {
    const result = this.u64(field);
    if (result === 0n) fail(`${field} must be non-zero.`);
    return result;
  }

  text8(field: string): string {
    const length = this.u8(`${field}.length`);
    if (length === 0) fail(`${field} must be non-empty.`);
    return decodeText(this.bytes(length, field), field);
  }

  text16(field: string): string {
    const length = this.u16(`${field}.length`);
    if (length === 0 || length > 4096) fail(`${field} has invalid length.`);
    return decodeText(this.bytes(length, field), field);
  }

  bytes(length: number, field: string): Buffer {
    this.require(length, field);
    const result = Buffer.from(this.buffer.subarray(this.offset, this.offset + length));
    this.offset += length;
    return result;
  }

  skipRemaining(): void {
    this.offset = this.buffer.byteLength;
  }

  end(): void {
    if (this.remaining !== 0) fail('Trailing bytes are forbidden.');
  }

  private require(length: number, field: string): void {
    if (length < 0 || this.remaining < length) fail(`Truncated ${field}.`);
  }
}

function decodeText(bytes: Buffer, field: string): string {
  const value = bytes.toString('utf8');
  if (value.includes('\uFFFD') || value.includes('\0') || Buffer.from(value).compare(bytes) !== 0) {
    fail(`${field} is not canonical UTF-8.`);
  }
  return value;
}

function fail(message: string): never {
  throw new ServiceWireProtocolError(message);
}
