import type {
  ActorRef,
  Message,
  RoutingId,
  ZlinkStreamHeader,
  ZLinkActor,
  ZLinkBoundSession,
  ZLinkBoundSessionSendCall,
  ZLinkSessionActor,
  ZLinkSessionActors,
  ZLinkSessionClient,
  ZLinkSessionContext,
  ZLinkSessionReplyCall,
  ZLinkSessionSendCall,
  ZLinkStream
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';

export interface ZLinkStreamBindingRuntimeOptions {
  readonly transport?: ZLinkBoundSessionTransport;
  readonly actorRefResolver?: (actor: ZLinkActor) => ActorRef;
  readonly relay?: (actor: ZLinkSessionActor, header: ZlinkStreamHeader, payload: Message, signal?: AbortSignal) => Promise<void>;
  readonly notifyDisconnected?: (actor: ZLinkSessionActor, signal?: AbortSignal) => Promise<void>;
}

export interface ZLinkBoundSessionTransport {
  send(actorId: string, message: unknown, options: ZLinkBoundSessionSendOptions): Promise<void>;
  disconnect(actorId: string, options: ZLinkBoundSessionDisconnectOptions): Promise<void>;
}

export interface ZLinkBoundSessionSendOptions {
  readonly bindingToken: string;
  readonly packetName?: string;
  readonly metadata: ReadonlyMap<string, string>;
  readonly signal?: AbortSignal;
}

export interface ZLinkBoundSessionDisconnectOptions {
  readonly bindingToken: string;
  readonly signal?: AbortSignal;
}

interface ZLinkActorSessionRoute {
  readonly context: DefaultZLinkSessionContext;
  readonly actor: DefaultZLinkSessionActor;
  readonly bindingToken: string;
}

export class ZLinkStreamBindingRuntime {
  private readonly routes = new Map<string, ZLinkActorSessionRoute>();

  constructor(private readonly options: ZLinkStreamBindingRuntimeOptions = {}) {}

  createSessionContext(stream: ZLinkStream, close?: (signal?: AbortSignal) => Promise<void>): DefaultZLinkSessionContext {
    return new DefaultZLinkSessionContext(this, stream, close ?? ((signal) => stream.close(signal)));
  }

  createBoundSession(actorId: string): ZLinkBoundSession {
    return new DefaultZLinkBoundSession(this, actorId);
  }

  bind(context: DefaultZLinkSessionContext, actorOrRef: ZLinkActor | ActorRef): DefaultZLinkSessionActor {
    const actorRef = isActorRef(actorOrRef)
      ? actorOrRef
      : this.resolveActorRef(actorOrRef);
    if (actorRef.actorId.trim().length === 0) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        'Actor id must not be empty.'
      );
    }
    if (context.routingId === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        'Actor session binding requires a stream routing id.'
      );
    }

    const bindingToken = createBindingToken();
    const sessionActor = new DefaultZLinkSessionActor(this, actorRef, bindingToken);
    this.routes.set(actorRef.actorId, { context, actor: sessionActor, bindingToken });
    context.bindLocal(sessionActor, bindingToken);
    return sessionActor;
  }

  find(actorId: string): DefaultZLinkSessionActor | undefined {
    return this.routes.get(actorId)?.actor;
  }

  unbind(actorId: string, context: DefaultZLinkSessionContext, bindingToken: string): void {
    const route = this.routes.get(actorId);
    if (route === undefined || route.context !== context || route.bindingToken !== bindingToken) {
      return;
    }
    this.routes.delete(actorId);
    context.unbindLocal(actorId, bindingToken);
  }

  cleanup(context: DefaultZLinkSessionContext): void {
    for (const route of [...this.routes.values()]) {
      if (route.context === context) {
        this.unbind(route.actor.actorId, context, route.bindingToken);
      }
    }
  }

  async sendBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const route = this.requireRoute(actorId);
    await this.requireTransport().send(actorId, message, {
      bindingToken: route.bindingToken,
      packetName,
      metadata,
      signal
    });
    this.requireCurrentToken(actorId, route.bindingToken);
  }

  async disconnectBoundSession(actorId: string, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const route = this.requireRoute(actorId);
    try {
      await this.requireTransport().disconnect(actorId, {
        bindingToken: route.bindingToken,
        signal
      });
    } finally {
      this.unbind(actorId, route.context, route.bindingToken);
    }
  }

  async relay(actor: DefaultZLinkSessionActor, header: ZlinkStreamHeader, payload: Message, signal?: AbortSignal): Promise<void> {
    this.requireCurrentToken(actor.actorId, actor.bindingToken);
    await this.options.relay?.(actor, header, payload, signal);
  }

  async notifyDisconnected(actor: DefaultZLinkSessionActor, signal?: AbortSignal): Promise<void> {
    this.requireCurrentToken(actor.actorId, actor.bindingToken);
    await this.options.notifyDisconnected?.(actor, signal);
  }

  private resolveActorRef(actor: ZLinkActor): ActorRef {
    if (this.options.actorRefResolver !== undefined) {
      return this.options.actorRefResolver(actor);
    }
    const state = actor.context as unknown as { actorRef?: ActorRef };
    if (state.actorRef !== undefined) {
      return state.actorRef;
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorRouteNotFound,
      `Actor '${actor.actorId}' does not have a concrete actor ref.`
    );
  }

  private requireRoute(actorId: string): ZLinkActorSessionRoute {
    const route = this.routes.get(actorId);
    if (route !== undefined) {
      return route;
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorSessionNotBound,
      `No current session binding exists for actor '${actorId}'.`,
      true
    );
  }

  private requireCurrentToken(actorId: string, bindingToken: string): void {
    const route = this.requireRoute(actorId);
    if (route.bindingToken === bindingToken) {
      return;
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorSessionNotBound,
      `Actor '${actorId}' session binding is stale.`,
      true
    );
  }

  private requireTransport(): ZLinkBoundSessionTransport {
    if (this.options.transport === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorSessionNotBound,
        'Bound session transport is not started.',
        true
      );
    }
    return this.options.transport;
  }
}

export class DefaultZLinkSessionContext implements ZLinkSessionContext {
  readonly client: ZLinkSessionClient;
  readonly actors: ZLinkSessionActors;
  private readonly localActors = new Map<string, { actor: DefaultZLinkSessionActor; token: string }>();

  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    readonly stream: ZLinkStream,
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
    this.runtime.cleanup(this);
    await this.closeSession(signal);
  }

  get boundActors(): readonly DefaultZLinkSessionActor[] {
    return [...this.localActors.values()].map((entry) => entry.actor);
  }

  bindLocal(actor: DefaultZLinkSessionActor, token: string): void {
    this.localActors.set(actor.actorId, { actor, token });
  }

  unbindLocal(actorId: string, token: string): void {
    const current = this.localActors.get(actorId);
    if (current?.token === token) {
      this.localActors.delete(actorId);
    }
  }
}

class DefaultZLinkSessionClient implements ZLinkSessionClient {
  constructor(private readonly context: DefaultZLinkSessionContext) {}

  send<TMessage>(message: TMessage): ZLinkSessionSendCall {
    return new DefaultZLinkSessionSendCall(this.context, message);
  }

  reply<TMessage>(message: TMessage): ZLinkSessionReplyCall {
    return new DefaultZLinkSessionReplyCall(this.context, message);
  }
}

class DefaultZLinkSessionActors implements ZLinkSessionActors {
  constructor(
    private readonly context: DefaultZLinkSessionContext,
    private readonly runtime: ZLinkStreamBindingRuntime
  ) {}

  get bound(): readonly ZLinkSessionActor[] {
    return this.context.boundActors;
  }

  async bind(actor: ZLinkActor | ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor> {
    throwIfAborted(signal);
    return this.runtime.bind(this.context, actor);
  }

  find(actorId: string): ZLinkSessionActor | undefined {
    return this.context.boundActors.find((actor) => actor.actorId === actorId);
  }
}

export class DefaultZLinkSessionActor implements ZLinkSessionActor {
  readonly actorId: string;

  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    readonly ref: ActorRef,
    readonly bindingToken: string
  ) {
    this.actorId = ref.actorId;
  }

  relay(header: ZlinkStreamHeader, payload: Message, signal?: AbortSignal): Promise<void> {
    return this.runtime.relay(this, header, payload, signal);
  }

  notifyDisconnected(signal?: AbortSignal): Promise<void> {
    return this.runtime.notifyDisconnected(this, signal);
  }
}

export class DefaultZLinkBoundSession implements ZLinkBoundSession {
  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    private readonly actorId: string
  ) {}

  send<TMessage>(message: TMessage): ZLinkBoundSessionSendCall {
    return new DefaultZLinkBoundSessionSendCall(this.runtime, this.actorId, message);
  }

  disconnect(signal?: AbortSignal): Promise<void> {
    return this.runtime.disconnectBoundSession(this.actorId, signal);
  }
}

class DefaultZLinkBoundSessionSendCall<TMessage> implements ZLinkBoundSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();

  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    private readonly actorId: string,
    private readonly message: TMessage
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  packetName(packetName: string): this {
    this.selectedPacketName = packetName;
    return this;
  }

  submit(signal?: AbortSignal): Promise<void> {
    return this.runtime.sendBoundSession(
      this.actorId,
      this.message,
      this.selectedPacketName,
      this.selectedMetadata,
      signal
    );
  }
}

class DefaultZLinkSessionSendCall<TMessage> implements ZLinkSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();
  private compressionEnabled = false;

  constructor(
    private readonly context: DefaultZLinkSessionContext,
    private readonly message: TMessage
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  packetName(packetName: string): this {
    this.selectedPacketName = packetName;
    return this;
  }

  compress(enabled = true): this {
    this.compressionEnabled = enabled;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const payload = JSON.stringify({
      kind: 'send',
      packetName: this.selectedPacketName,
      metadata: Object.fromEntries(this.selectedMetadata),
      compressed: this.compressionEnabled,
      message: this.message
    });
    const message = createJsonMessage(payload);
    try {
      this.context.stream.write(message);
    } finally {
      message.close();
    }
  }
}

class DefaultZLinkSessionReplyCall<TMessage> implements ZLinkSessionReplyCall {
  private readonly selectedMetadata = new Map<string, string>();
  private compressionEnabled = false;

  constructor(
    private readonly context: DefaultZLinkSessionContext,
    private readonly message: TMessage
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  compress(enabled = true): this {
    this.compressionEnabled = enabled;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const payload = JSON.stringify({
      kind: 'reply',
      metadata: Object.fromEntries(this.selectedMetadata),
      compressed: this.compressionEnabled,
      message: this.message
    });
    const message = createJsonMessage(payload);
    try {
      this.context.stream.write(message);
    } finally {
      message.close();
    }
  }
}

function isActorRef(value: ZLinkActor | ActorRef): value is ActorRef {
  return (
    typeof value === 'object'
    && value !== null
    && 'nodeRid' in value
    && 'generation' in value
    && 'actorId' in value
  );
}

function createBindingToken(): string {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
}

function createJsonMessage(payload: string): Message {
  const binding = require('@zlink-systems/zlink') as { Message: { from(value: string): Message } };
  return binding.Message.from(payload);
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted) {
    throw new Error('The operation was aborted.');
  }
}
