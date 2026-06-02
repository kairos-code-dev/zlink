import type {
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkHandlerContext,
  ZLinkHandlerFilter,
  ZLinkPublishCall,
  ZLinkRequestCall,
  ZLinkSendCall
} from '../../contracts';
import { invokeZLinkHandlerFilters } from '../handlers';
import type {
  DealerSocket,
  Message,
  MessageLike,
  PubSocket
} from '@zlink-systems/zlink';
import { ZLinkConfigurationException, type ZLinkFrameworkRegistration } from '../configuration';

const CHANNEL_ENVELOPE_FORMAT = 'zlink.channel.v1';

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

  async send(_channelName: string, _packetName: string | undefined, message: unknown): Promise<void> {
    this.dealer.send().message(encodeChannelEnvelope(_packetName, message)).submit();
  }

  async request<TReply>(
    _channelName: string,
    _packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined
  ): Promise<TReply> {
    const operation = this.dealer.request().message(encodeChannelEnvelope(_packetName, request));
    if (timeoutMs !== undefined) {
      operation.timeout(timeoutMs);
    }
    return operation.submitAsync() as Promise<TReply>;
  }

  async publish(_channelName: string, topic: string, _packetName: string | undefined, event: unknown): Promise<void> {
    if (this.publisher === undefined) {
      throw new ZLinkConfigurationException('Channel publisher runtime is not started.');
    }
    this.publisher.publish(topic).message(encodeChannelEnvelope(_packetName, event)).submit();
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
    reply(routingId: unknown, requestSeq: bigint): { message(message: MessageLike): { submit(): void } };
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
    router.reply(received.routingId, received.requestSeq).message(toMessageLike(reply ?? Buffer.alloc(0))).submit();
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

function toMessageLike(value: unknown): MessageLike {
  if (typeof value === 'string' || Buffer.isBuffer(value) || value instanceof Uint8Array || isMessage(value)) {
    return value;
  }
  throw new TypeError('ZLink channel payload must be a Message, Buffer, Uint8Array, or string until codecs are registered.');
}

function isMessage(value: unknown): value is Message {
  return typeof value === 'object' && value !== null && typeof (value as { data?: unknown }).data === 'function';
}

function encodeChannelEnvelope(packetName: string | undefined, payload: unknown): MessageLike {
  const message = toMessageLike(payload);
  const bytes = isMessage(message) ? message.data() : Buffer.from(message);
  return JSON.stringify({
    format: CHANNEL_ENVELOPE_FORMAT,
    packetName,
    payload: bytes.toString('base64')
  });
}

function decodeChannelEnvelope(parts: readonly Message[]): ZLinkChannelEnvelope {
  if (parts.length !== 1) {
    throw new ZLinkConfigurationException('Channel envelope requires exactly one message part.');
  }
  const envelope = JSON.parse(parts[0].data().toString()) as { format?: string; packetName?: string; payload?: string };
  if (envelope.format !== CHANNEL_ENVELOPE_FORMAT || typeof envelope.payload !== 'string') {
    throw new ZLinkConfigurationException('Channel envelope header format is not supported.');
  }
  return { packetName: envelope.packetName, payload: Buffer.from(envelope.payload, 'base64') };
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
