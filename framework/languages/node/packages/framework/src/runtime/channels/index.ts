import type {
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkHandlerContext,
  ZLinkHandlerFilter,
  ZLinkPublishCall,
  ZLinkRequestCall,
  ZLinkSendCall
} from '../../contracts';
import { randomUUID } from 'node:crypto';
import { invokeZLinkHandlerFilters } from '../handlers';
import type {
  DealerSocket,
  Message,
  MessageLike,
  PubSocket
} from '@zlink-systems/zlink';
import { ZLinkConfigurationException, type ZLinkFrameworkRegistration } from '../configuration';

const JSON_CONTENT_TYPE = 'application/json';

const enum ZLinkChannelMessageKind {
  Request = 1,
  Response = 2,
  Command = 3,
  Publish = 4,
  Error = 5
}

interface ZLinkChannelEnvelopeHeader {
  readonly kind: ZLinkChannelMessageKind;
  readonly channelName: string;
  readonly messageName: string;
  readonly contentType: string;
  readonly correlationId: string | null;
  readonly deadline: string | null;
  readonly topic: string | null;
  readonly errorCode: string | null;
  readonly errorMessage: string | null;
  readonly source?: string | null;
}

export interface ZLinkChannelClientTransport {
  send(channelName: string, packetName: string | undefined, message: unknown, signal?: AbortSignal): Promise<void>;
  request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply>;
  publish(channelName: string, topic: string, packetName: string | undefined, event: unknown, signal?: AbortSignal): Promise<void>;
}

export class ZLinkDealerChannelClientTransport implements ZLinkChannelClientTransport {
  constructor(
    private readonly dealer: DealerSocket,
    private readonly publisher?: PubSocket
  ) {}

  async send(channelName: string, packetName: string | undefined, message: unknown): Promise<void> {
    appendParts(
      this.dealer.send(),
      encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, channelName, packetName, message)
    ).submit();
  }

  async request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined
  ): Promise<TReply> {
    const operation = appendParts(
      this.dealer.request(),
      encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, channelName, packetName, request, timeoutMs)
    );
    if (timeoutMs !== undefined) {
      operation.timeout(timeoutMs);
    }
    const reply = await operation.submitAsync();
    return decodeChannelReply<TReply>(reply);
  }

  async publish(channelName: string, topic: string, packetName: string | undefined, event: unknown): Promise<void> {
    if (this.publisher === undefined) {
      throw new ZLinkConfigurationException('Channel publisher runtime is not started.');
    }
    appendParts(
      this.publisher.publish(topic),
      encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Publish, channelName, packetName, event, undefined, topic)
    ).submit();
  }
}

export interface ZLinkChannelEnvelope {
  readonly packetName?: string;
  readonly payload: Buffer;
}

export interface ZLinkChannelRequestDispatcherOptions {
  readonly handlers: ReadonlyMap<string, ZLinkChannelRequestHandler>;
  readonly filters?: readonly ZLinkHandlerFilter[];
}

export interface ZLinkChannelRequestHandler {
  handle(payload: Buffer, context: ZLinkHandlerContext): Promise<unknown> | unknown;
}

export class ZLinkChannelRequestDispatcher {
  private readonly filters: readonly ZLinkHandlerFilter[];

  constructor(private readonly options: ZLinkChannelRequestDispatcherOptions) {
    this.filters = options.filters ?? [];
  }

  async dispatch(received: { parts: readonly Message[]; routingId: unknown; requestSeq: bigint | null }, router: {
    reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
  }): Promise<void> {
    const envelope = decodeChannelEnvelope(received.parts);
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Channel request is missing packetName.');
    }
    const handler = this.options.handlers.get(packetName);
    if (handler === undefined) {
      throw new ZLinkConfigurationException(`No channel request handler is registered for packet '${packetName}'.`);
    }
    if (received.requestSeq === null) {
      throw new ZLinkConfigurationException('Channel request cannot be replied to because requestSeq is missing.');
    }

    const context: ZLinkHandlerContext = { packetName };
    const reply = await invokeZLinkHandlerFilters(
      this.filters,
      { context, handler },
      () => Promise.resolve(handler.handle(envelope.payload, context))
    );
    appendParts(
      router.reply(received.routingId, received.requestSeq),
      encodeChannelReplyParts(envelope.header, reply)
    ).submit();
  }
}

export class DefaultZLinkChannelClient implements ZLinkChannelClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkChannelClientTransport
  ) {}

  send<TMessage>(channelNameOrMessage: string | TMessage, maybeMessage?: TMessage): ZLinkSendCall {
    const channelName = typeof channelNameOrMessage === 'string' ? channelNameOrMessage : '';
    const message = typeof channelNameOrMessage === 'string' ? maybeMessage : channelNameOrMessage;
    return new DefaultZLinkSendCall(
      () => this.requireClientChannel(channelName),
      (packetName, signal) => this.requireTransport().send(channelName, packetName, message, signal)
    );
  }

  request<TRequest>(channelNameOrRequest: string | TRequest, maybeRequest?: TRequest): ZLinkRequestCall {
    const channelName = typeof channelNameOrRequest === 'string' ? channelNameOrRequest : '';
    const request = typeof channelNameOrRequest === 'string' ? maybeRequest : channelNameOrRequest;
    return new DefaultZLinkRequestCall(
      () => this.requireClientChannel(channelName),
      (packetName, timeoutMs, signal) => this.requireTransport().request(channelName, packetName, request, timeoutMs, signal)
    );
  }

  sendToChannel<TMessage>(channelName: string, message: TMessage): ZLinkSendCall {
    return this.send(channelName, message);
  }

  requestToChannel<TRequest>(channelName: string, request: TRequest): ZLinkRequestCall {
    return this.request(channelName, request);
  }

  private requireClientChannel(channelName: string): void {
    if (!this.registration.channelClients.has(channelName)) {
      throw new ZLinkConfigurationException(`Channel '${channelName}' does not have a client capability.`);
    }
  }

  private requireTransport(): ZLinkChannelClientTransport {
    if (this.transport === undefined) {
      throw new ZLinkConfigurationException('Channel runtime is not started.');
    }
    return this.transport;
  }
}

interface ZLinkMultipartOperation<TNext> {
  message(message: MessageLike): TNext;
}

interface ZLinkMultipartSubmitOperation extends ZLinkMultipartOperation<ZLinkMultipartSubmitOperation> {
  submit(): unknown;
}

interface ZLinkMultipartRequestOperation extends ZLinkMultipartOperation<ZLinkMultipartRequestOperation> {
  timeout(timeoutMs: number): ZLinkMultipartRequestOperation;
  submitAsync(): Promise<Message[]>;
}

type ZLinkMultipartReplyOperation = ZLinkMultipartSubmitOperation;

function appendParts<TNext extends ZLinkMultipartOperation<TNext>>(
  operation: ZLinkMultipartOperation<TNext>,
  parts: readonly MessageLike[]
): TNext {
  if (parts.length === 0) {
    throw new ZLinkConfigurationException('Channel multipart envelope must contain at least one part.');
  }
  let current: TNext = operation.message(parts[0]);
  for (let index = 1; index < parts.length; index++) {
    current = current.message(parts[index]);
  }
  return current;
}

function toMessageLike(value: unknown): MessageLike {
  if (Buffer.isBuffer(value) || value instanceof Uint8Array || isMessage(value)) {
    return value;
  }
  return encodeJsonBytes(value);
}

function isMessage(value: unknown): value is Message {
  return typeof value === 'object' && value !== null && typeof (value as { data?: unknown }).data === 'function';
}

function encodeChannelEnvelopeParts(
  kind: ZLinkChannelMessageKind,
  channelName: string,
  packetName: string | undefined,
  payload: unknown,
  timeoutMs?: number,
  topic?: string
): readonly MessageLike[] {
  const header: ZLinkChannelEnvelopeHeader = {
    kind,
    channelName,
    messageName: packetName ?? resolveChannelPacketName(payload),
    contentType: JSON_CONTENT_TYPE,
    correlationId: randomUUID().replaceAll('-', ''),
    deadline: timeoutMs === undefined ? null : new Date(Date.now() + timeoutMs).toISOString(),
    topic: topic ?? null,
    errorCode: null,
    errorMessage: null
  };
  return [encodeJsonBytes(header), toMessageLike(payload)];
}

function encodeChannelReplyParts(request: ZLinkChannelEnvelopeHeader, payload: unknown): readonly MessageLike[] {
  const header: ZLinkChannelEnvelopeHeader = {
    kind: ZLinkChannelMessageKind.Response,
    channelName: request.channelName,
    messageName: request.messageName,
    contentType: JSON_CONTENT_TYPE,
    correlationId: request.correlationId,
    deadline: null,
    topic: null,
    errorCode: null,
    errorMessage: null
  };
  return [encodeJsonBytes(header), toMessageLike(payload ?? Buffer.alloc(0))];
}

function decodeChannelReply<TReply>(parts: readonly Message[]): TReply {
  const header = decodeChannelHeader(parts);
  if (header.kind === ZLinkChannelMessageKind.Error) {
    throw new ZLinkConfigurationException(header.errorMessage ?? 'ZLink channel request failed.');
  }
  if (header.kind !== ZLinkChannelMessageKind.Response) {
    throw new ZLinkConfigurationException(`Channel reply kind '${header.kind}' is not a response.`);
  }
  if (parts.length < 2 || parts[1].data().length === 0) {
    return undefined as TReply;
  }
  return JSON.parse(parts[1].data().toString()) as TReply;
}

function decodeChannelEnvelope(parts: readonly Message[]): ZLinkChannelEnvelope & { readonly header: ZLinkChannelEnvelopeHeader } {
  const header = decodeChannelHeader(parts);
  if (parts.length < 2) {
    throw new ZLinkConfigurationException('Channel envelope body part is missing.');
  }
  return { header, packetName: header.messageName, payload: Buffer.from(parts[1].data()) };
}

function decodeChannelHeader(parts: readonly Message[]): ZLinkChannelEnvelopeHeader {
  if (parts.length === 0) {
    throw new ZLinkConfigurationException('Channel envelope header part is missing.');
  }
  return JSON.parse(parts[0].data().toString()) as ZLinkChannelEnvelopeHeader;
}

function encodeJsonBytes(value: unknown): Buffer {
  return Buffer.from(JSON.stringify(value ?? null));
}

function resolveChannelPacketName(payload: unknown): string {
  if (payload !== null && typeof payload === 'object' && 'constructor' in payload) {
    const name = (payload as { constructor?: { name?: string } }).constructor?.name;
    if (name !== undefined && name !== 'Object') {
      return name;
    }
  }
  throw new ZLinkConfigurationException('Channel packetName is required when the payload type cannot provide one.');
}

export class DefaultZLinkFanoutClient implements ZLinkFanoutClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkChannelClientTransport
  ) {}

  publish<TEvent>(topicOrChannelName: string, eventOrTopic: TEvent | string, maybeEvent?: TEvent): ZLinkPublishCall {
    const channelName = maybeEvent === undefined ? '' : topicOrChannelName;
    const topic = maybeEvent === undefined ? topicOrChannelName : String(eventOrTopic);
    const event = maybeEvent === undefined ? eventOrTopic : maybeEvent;
    return new DefaultZLinkPublishCall(
      () => this.requirePublisherChannel(channelName),
      (packetName, signal) => this.requireTransport().publish(channelName, topic, packetName, event, signal)
    );
  }

  publishToChannel<TEvent>(channelName: string, topic: string, event: TEvent): ZLinkPublishCall {
    return this.publish(channelName, topic, event);
  }

  private requirePublisherChannel(channelName: string): void {
    if (!this.registration.fanoutPublishers.has(channelName)) {
      throw new ZLinkConfigurationException(`Channel '${channelName}' does not have a publisher capability.`);
    }
  }

  private requireTransport(): ZLinkChannelClientTransport {
    if (this.transport === undefined) {
      throw new ZLinkConfigurationException('Channel runtime is not started.');
    }
    return this.transport;
  }
}

class DefaultZLinkSendCall implements ZLinkSendCall {
  private packet?: string;

  constructor(
    private readonly validate: () => void,
    private readonly submitter: (packetName: string | undefined, signal?: AbortSignal) => Promise<void>
  ) {}

  packetName(packetName: string): this {
    this.packet = packetName;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<void> {
    this.validate();
    await this.submitter(this.packet, signal);
  }
}

class DefaultZLinkRequestCall implements ZLinkRequestCall {
  private packet?: string;
  private timeoutMs?: number;

  constructor(
    private readonly validate: () => void,
    private readonly submitter: <TReply>(
      packetName: string | undefined,
      timeoutMs: number | undefined,
      signal?: AbortSignal
    ) => Promise<TReply>
  ) {}

  packetName(packetName: string): this {
    this.packet = packetName;
    return this;
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  async submit<TReply>(signal?: AbortSignal): Promise<TReply> {
    this.validate();
    return this.submitter<TReply>(this.packet, this.timeoutMs, signal);
  }
}

class DefaultZLinkPublishCall implements ZLinkPublishCall {
  private packet?: string;

  constructor(
    private readonly validate: () => void,
    private readonly submitter: (packetName: string | undefined, signal?: AbortSignal) => Promise<void>
  ) {}

  packetName(packetName: string): this {
    this.packet = packetName;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<void> {
    this.validate();
    await this.submitter(this.packet, signal);
  }
}
