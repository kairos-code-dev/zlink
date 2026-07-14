import type {
  ActorRef,
  RoutingId,
  ZLinkMessage,
  ZLinkActor,
  ZLinkBoundSession,
  ZLinkSessionActor,
  ZLinkSessionActors,
  ZLinkSessionClient,
  ZLinkSessionContext,
  ZLinkSessionHandlerRegistry,
  ZLinkSessionPacketHandler,
  ZLinkSessionReplyCall,
  ZLinkSessionSendCall,
  ZLinkStream
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { readZLinkDecoratorMetadata } from '../../contracts/Handlers/Attributes';
import { throwIfAborted } from '../abort';
import { ZLinkConfigurationException } from '../configuration';
import {
  messageToBytes,
  type ZLinkStreamFrameHeader,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind,
  type ZLinkStreamReplyMessageKind
} from './protocol';
import {
  DefaultZLinkBoundSessionSendCall,
  DefaultZLinkSessionReplyCall,
  DefaultZLinkSessionSendCall
} from './session-calls';
import {
  ZLinkSessionRequestTracker,
  type ZLinkPendingSessionRequest
} from './session-requests';
import { ZLinkSessionLocalActorBindings } from './session-local-actors';

export interface ZLinkSessionContextStream extends ZLinkStream {
  writeRaw(payload: Message, flags?: number): boolean;
}

interface ZLinkSessionContextRuntime {
  cleanup(context: DefaultZLinkSessionContext): Promise<void>;
  createTextMessage(payload: string): Message;
  createBinaryMessage(payload: Uint8Array): Message;
  createJsonFrameMessage(
    kind: ZLinkStreamMessageKind,
    packetName: string,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    requestSeq: bigint | undefined,
    payload: unknown,
    correlationId?: string
  ): Message;
  createJsonReplyFrameMessage(
    requestHeader: ZLinkStreamFrameHeader,
    kind: ZLinkStreamReplyMessageKind,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    payload: unknown
  ): Message;
  decompressPayload(payload: Message): Message;
  bind(
    context: DefaultZLinkSessionContext,
    actor: ZLinkActor | ActorRef,
    signal?: AbortSignal
  ): Promise<DefaultZLinkSessionActor>;
  bindOrGet(
    context: DefaultZLinkSessionContext,
    actorRef: ActorRef,
    signal?: AbortSignal
  ): Promise<DefaultZLinkSessionActor>;
  relay(actor: DefaultZLinkSessionActor, payload: ZLinkMessage, signal?: AbortSignal): Promise<void>;
  notifyDisconnected(actor: DefaultZLinkSessionActor, signal?: AbortSignal): Promise<void>;
}

interface ZLinkBoundSessionRuntime {
  sendBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void>;
  disconnectBoundSession(actorId: string, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkBoundSessionFactory {
  create(actorId: string): ZLinkBoundSession;
}

export class DefaultZLinkSessionContext implements ZLinkSessionContext {
  readonly client: ZLinkSessionClient;
  readonly actors: ZLinkSessionActors;
  private readonly handlerRegistry = new DefaultZLinkSessionHandlerRegistry(this);
  readonly handlers: ZLinkSessionHandlerRegistry = this.handlerRegistry;
  private readonly localActors = new ZLinkSessionLocalActorBindings<DefaultZLinkSessionActor>();
  private readonly requests = new ZLinkSessionRequestTracker();
  private currentDispatchHeader: ZLinkStreamFrameHeader | undefined;

  constructor(
    private readonly runtime: ZLinkSessionContextRuntime,
    readonly stream: ZLinkSessionContextStream,
    private readonly closeSession: (signal?: AbortSignal) => Promise<void>
  ) {
    this.client = new DefaultZLinkSessionClient(this);
    this.actors = new DefaultZLinkSessionActors(this, runtime);
  }

  get sessionId(): string {
    return this.stream.sessionId;
  }

  get routingId(): RoutingId | undefined {
    return this.stream.routingId;
  }

  get localAddr(): string | undefined {
    return this.stream.localAddr;
  }

  get remoteAddr(): string | undefined {
    return this.stream.remoteAddr;
  }

  async close(signal?: AbortSignal): Promise<void> {
    await this.runtime.cleanup(this);
    await this.closeSession(signal);
  }

  async cleanupBindings(): Promise<void> {
    await this.runtime.cleanup(this);
  }

  createTextMessage(payload: string): Message {
    return this.runtime.createTextMessage(payload);
  }

  createBinaryMessage(payload: Uint8Array): Message {
    return this.runtime.createBinaryMessage(payload);
  }

  createJsonFrameMessage(
    kind: ZLinkStreamMessageKind,
    packetName: string,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    requestSeq: bigint | undefined,
    payload: unknown,
    correlationId?: string
  ): Message {
    return this.runtime.createJsonFrameMessage(
      kind,
      packetName,
      metadata,
      compressed,
      requestSeq,
      payload,
      correlationId
    );
  }

  createJsonReplyFrameMessage(
    requestHeader: ZLinkStreamFrameHeader,
    kind: ZLinkStreamReplyMessageKind,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    payload: unknown
  ): Message {
    return this.runtime.createJsonReplyFrameMessage(
      requestHeader,
      kind,
      metadata,
      compressed,
      payload
    );
  }

  enterDispatch(header: ZLinkStreamFrameHeader): void {
    this.currentDispatchHeader = header;
  }

  exitDispatch(): void {
    this.currentDispatchHeader = undefined;
  }

  get dispatchHeader(): ZLinkStreamFrameHeader | undefined {
    return this.currentDispatchHeader;
  }

  startRequest(timeoutMs?: number): ZLinkPendingSessionRequest {
    return this.requests.start(timeoutMs);
  }

  tryCompleteResponse(header: ZLinkStreamFrameHeader, payload: Message): boolean {
    if (header.requestSeq === undefined) {
      return false;
    }
    if (header.kind === ZLinkStreamMessageKind.Response) {
      return this.requests.complete(header.requestSeq, payload);
    }
    if (header.kind === ZLinkStreamMessageKind.Error) {
      return this.requests.fail(header.requestSeq, decodeStreamErrorPayload(payload));
    }
    return false;
  }

  payloadForHeader(header: ZLinkStreamFrameHeader, payload: Message): Message {
    if ((header.flags & ZLinkStreamHeaderFlags.PayloadCompressed) === 0) {
      return payload;
    }
    return this.runtime.decompressPayload(payload);
  }

  get boundActors(): readonly DefaultZLinkSessionActor[] {
    return this.localActors.snapshot();
  }

  findBoundActor(actorId: string): DefaultZLinkSessionActor | undefined {
    return this.localActors.find(actorId);
  }

  bindLocal(actor: DefaultZLinkSessionActor, token: string): void {
    this.localActors.bind(actor, token);
  }

  unbindLocal(actorId: string, token: string): void {
    this.localActors.unbind(actorId, token);
  }

  completeConfiguration(): void {
    this.handlerRegistry.closeRegistration();
  }
}

class DefaultZLinkSessionHandlerRegistry implements ZLinkSessionHandlerRegistry {
  private readonly handlersByPacket = new Map<string, ZLinkSessionPacketHandler<ZLinkSessionContext>>();
  private registrationOpen = true;

  constructor(private readonly context: ZLinkSessionContext) {}

  addHandler<THandler>(handlerType: new (...args: never[]) => THandler): this {
    if (!this.registrationOpen) {
      throw new ZLinkConfigurationException('Session handler registration is closed.');
    }
    const packetName = sessionHandlerPacketName(handlerType);
    if (this.handlersByPacket.has(packetName)) {
      throw new ZLinkConfigurationException(
        `Session packet '${packetName}' is already registered.`
      );
    }
    const handler = new handlerType() as THandler & Partial<ZLinkSessionPacketHandler<ZLinkSessionContext>>;
    if (typeof handler.handle !== 'function') {
      throw new TypeError(`Session handler '${handlerType.name}' must implement handle(...).`);
    }
    this.handlersByPacket.set(packetName, handler as ZLinkSessionPacketHandler<ZLinkSessionContext>);
    return this;
  }

  closeRegistration(): void {
    this.registrationOpen = false;
  }

  async tryHandle(dispatch: import('../../contracts').ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<boolean> {
    const handler = this.handlersByPacket.get(dispatch.packetName);
    if (handler === undefined) {
      return false;
    }
    await handler.handle(this.context, dispatch, payload);
    return true;
  }
}

function sessionHandlerPacketName(handlerType: new (...args: never[]) => unknown): string {
  const declared = readZLinkDecoratorMetadata(handlerType)
    .find((metadata) => metadata.kind === 'packet')?.packetName?.trim();
  if (declared === undefined || declared.length === 0) {
    throw new ZLinkConfigurationException(
      `Session handler '${handlerType.name}' must declare a packet name with @ZLinkPacket(...).`
    );
  }
  return declared;
}

class DefaultZLinkSessionClient implements ZLinkSessionClient {
  constructor(private readonly context: DefaultZLinkSessionContext) {}

  send(message: unknown): ZLinkSessionSendCall {
    return new DefaultZLinkSessionSendCall(this.context, message);
  }

  reply(message: unknown): ZLinkSessionReplyCall {
    return new DefaultZLinkSessionReplyCall(this.context, message);
  }
}

class DefaultZLinkSessionActors implements ZLinkSessionActors {
  constructor(
    private readonly context: DefaultZLinkSessionContext,
    private readonly runtime: ZLinkSessionContextRuntime
  ) {}

  get bound(): readonly ZLinkSessionActor[] {
    return this.context.boundActors;
  }

  async bind(actor: ZLinkActor | ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor> {
    throwIfAborted(signal);
    return await this.runtime.bind(this.context, actor, signal);
  }

  async bindOrGet(actor: ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor> {
    throwIfAborted(signal);
    return await this.runtime.bindOrGet(this.context, actor, signal);
  }

  find(actorId: string): ZLinkSessionActor | undefined {
    return this.context.findBoundActor(actorId);
  }
}

export class DefaultZLinkSessionActor implements ZLinkSessionActor {
  readonly actorId: string;
  private currentRef: ActorRef;

  constructor(
    private readonly runtime: ZLinkSessionContextRuntime,
    ref: ActorRef,
    readonly bindingToken: string
  ) {
    this.actorId = ref.actorId;
    this.currentRef = ref;
  }

  get ref(): ActorRef {
    return this.currentRef;
  }

  updateRef(ref: ActorRef): void {
    if (ref.actorId !== this.actorId) {
      throw new Error(`Cannot change session actor id from '${this.actorId}' to '${ref.actorId}'.`);
    }
    this.currentRef = ref;
  }

  relay(payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    return this.runtime.relay(this, payload, signal);
  }

  notifyDisconnected(signal?: AbortSignal): Promise<void> {
    return this.runtime.notifyDisconnected(this, signal);
  }
}

export class DefaultZLinkBoundSession implements ZLinkBoundSession {
  constructor(
    private readonly runtime: ZLinkBoundSessionRuntime,
    private readonly actorId: string
  ) {}

  send(message: unknown): DefaultZLinkBoundSessionSendCall {
    return new DefaultZLinkBoundSessionSendCall(this.runtime, this.actorId, message);
  }

  disconnect(signal?: AbortSignal): Promise<void> {
    return this.runtime.disconnectBoundSession(this.actorId, signal);
  }
}

export class DefaultZLinkBoundSessionFactory implements ZLinkBoundSessionFactory {
  constructor(private readonly runtime: ZLinkBoundSessionRuntime) {}

  create(actorId: string): DefaultZLinkBoundSession {
    return new DefaultZLinkBoundSession(this.runtime, actorId);
  }
}

function decodeStreamErrorPayload(payload: Message): Error {
  try {
    const value = JSON.parse(Buffer.from(messageToBytes(payload)).toString()) as {
      readonly code?: unknown;
      readonly message?: unknown;
    };
    return new Error(typeof value.message === 'string' ? value.message : 'Stream request failed.');
  } catch {
    return new Error('Stream request failed.');
  }
}
