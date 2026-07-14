import type {
  Type,
  ZLinkActor,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotActorReplyOptions,
  ZLinkSpotActorSendContext,
  ZLinkSpotActorSendHandler
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkMessageMetadataEmpty
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { ZLinkConfigurationException } from '../configuration';
import { wrapFrameworkPayloadMessage } from '../messaging/payload-codec';
import type { ZLinkMessageSerializer } from '../../contracts';

export enum ZLinkActorPacketKind {
  Send = 'send',
  Request = 'request'
}

export interface ZLinkActorPacketDescriptor {
  readonly kind: ZLinkActorPacketKind;
  readonly packetName: string;
  readonly actorType: Type<ZLinkActor>;
  readonly handlerType: Type;
}

export class ZLinkSpotActorHandlerRegistryRuntime {
  private readonly packets = new Map<string, ZLinkActorPacketDescriptor>();

  addPacket(descriptor: ZLinkActorPacketDescriptor): this {
    const key = packetKey(descriptor.kind, descriptor.actorType, descriptor.packetName);
    if (this.packets.has(key)) {
      throw new ZLinkConfigurationException(
        `Actor packet '${descriptor.packetName}' for '${descriptor.actorType.name}' is already registered.`
      );
    }
    this.packets.set(key, descriptor);
    return this;
  }

  resolvePacket(
    kind: ZLinkActorPacketKind,
    actor: ZLinkActor,
    packetName: string
  ): ZLinkActorPacketDescriptor | undefined {
    const actorType = actor.constructor as Type<ZLinkActor>;
    const exact = this.packets.get(packetKey(kind, actorType, packetName));
    if (exact !== undefined) {
      return exact;
    }
    const wildcard = this.packets.get(packetKey(kind, Object as unknown as Type<ZLinkActor>, packetName));
    if (wildcard !== undefined) {
      return wildcard;
    }

    for (const descriptor of this.packets.values()) {
      if (
        descriptor.kind === kind
        && descriptor.packetName === packetName
        && actor instanceof descriptor.actorType
      ) {
        return descriptor;
      }
    }
    return undefined;
  }

}

export interface ZLinkSpotActorDispatcherOptions {
  readonly registry: ZLinkSpotActorHandlerRegistryRuntime;
  readonly spot: ZLinkSpot;
  readonly handlerFactory?: (handlerType: Type) => unknown;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly serial?: { execute<T>(operation: () => Promise<T> | T): Promise<T> };
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
}

export class DefaultZLinkSpotActorReplyOptions implements ZLinkSpotActorReplyOptions {
  private readonly selectedMetadata = new Map<string, string>();
  private compressionEnabled = false;

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  compress(enabled = true): this {
    this.compressionEnabled = enabled;
    return this;
  }

  snapshot(): ZLinkSpotActorReplyOptionsSnapshot {
    return {
      metadata: new Map(this.selectedMetadata),
      compressPayload: this.compressionEnabled
    };
  }
}

export interface ZLinkSpotActorReplyOptionsSnapshot {
  readonly metadata: ReadonlyMap<string, string>;
  readonly compressPayload: boolean;
}

export class ZLinkSpotActorDispatcher {
  constructor(private readonly options: ZLinkSpotActorDispatcherOptions) {}

  dispatchSend<TMessage>(
    actor: ZLinkActor,
    packetName: string,
    message: TMessage,
    context: Partial<ZLinkSpotActorSendContext> = {}
  ): Promise<void> {
    return this.execute(async () => {
      const descriptor = this.requirePacket(ZLinkActorPacketKind.Send, actor, packetName);
      const handler = this.createHandler<ZLinkSpotActorSendHandler<ZLinkSpot, ZLinkActor, TMessage>>(descriptor);
      await handler.handle(this.options.spot, actor, this.createSendContext(packetName, context), message);
    });
  }

  dispatchRequest<TRequest, TReply>(
    actor: ZLinkActor,
    packetName: string,
    request: TRequest,
    context: Partial<ZLinkSpotActorRequestContext> = {}
  ): Promise<TReply> {
    return this.dispatchRequestThen<TRequest, TReply, TReply>(actor, packetName, request, context, (reply) => reply);
  }

  dispatchRequestThen<TRequest, TReply, TResult>(
    actor: ZLinkActor,
    packetName: string,
    request: TRequest,
    context: Partial<ZLinkSpotActorRequestContext> = {},
    afterReply: (reply: TReply, options: ZLinkSpotActorReplyOptionsSnapshot) => Promise<TResult> | TResult
  ): Promise<TResult> {
    return this.execute(async () => {
      const descriptor = this.requirePacket(ZLinkActorPacketKind.Request, actor, packetName);
      const handler = this.createHandler<ZLinkSpotActorRequestHandler<ZLinkSpot, ZLinkActor, TRequest, TReply>>(descriptor);
      const replyOptions = context.reply instanceof DefaultZLinkSpotActorReplyOptions
        ? context.reply
        : new DefaultZLinkSpotActorReplyOptions();
      const requestContext = {
        ...context,
        reply: context.reply ?? replyOptions
      };
      const reply = await handler.handle(
        this.options.spot,
        actor,
        this.createRequestContext(packetName, requestContext),
        request
      );
      const optionsSnapshot = context.reply === undefined || context.reply instanceof DefaultZLinkSpotActorReplyOptions
        ? replyOptions.snapshot()
        : { metadata: new Map<string, string>(), compressPayload: false };
      return await afterReply(reply, optionsSnapshot);
    });
  }

  admitActorJoin(
    actor: ZLinkActor,
    request: Message,
    commit: () => Promise<void> | void
  ): Promise<ZLinkSpotActorJoinResponse> {
    return this.evaluateActorJoin(actor, request).then(async (result) => {
      if (result.accepted) {
        await this.commitActorJoin(actor, commit);
      }
      return result;
    });
  }

  evaluateActorJoin(
    actor: ZLinkActor,
    request: Message
  ): Promise<ZLinkSpotActorJoinResponse> {
    return this.execute(async () => {
      const payload = wrapFrameworkPayloadMessage(request, this.options.messageSerializers);
      const onActorJoin = (this.options.spot as Partial<ZLinkSpot>).onActorJoin;
      return onActorJoin === undefined
        ? { accepted: false }
        : await onActorJoin.call(this.options.spot, actor.actorId, payload);
    });
  }

  commitActorJoin(
    actor: ZLinkActor,
    commit: () => Promise<void> | void
  ): Promise<void> {
    return this.execute(async () => {
      await commit();
      await this.options.spot.onJoinedActor(actor);
    });
  }

  notifyJoinActor(actor: ZLinkActor): Promise<void> {
    return this.execute(() => this.options.spot.onJoinedActor(actor));
  }

  notifyLeaveActor(actor: ZLinkActor): Promise<void> {
    return this.execute(() => this.options.spot.onLeaveActor(actor));
  }

  notifyDisconnectActor(actor: ZLinkActor): Promise<void> {
    return this.execute(() => this.options.spot.onDisconnectActor?.(actor));
  }

  private requirePacket(
    kind: ZLinkActorPacketKind,
    actor: ZLinkActor,
    packetName: string
  ): ZLinkActorPacketDescriptor {
    const descriptor = this.options.registry.resolvePacket(kind, actor, packetName);
    if (descriptor === undefined) {
      throw actorDispatchHandlerNotFound(`No Spot actor ${kind} handler is registered for '${packetName}'.`);
    }
    this.ensureActorType(descriptor, actor);
    return descriptor;
  }

  private ensureActorType(descriptor: ZLinkActorPacketDescriptor, actor: ZLinkActor): void {
    if (descriptor.actorType === (Object as unknown as Type<ZLinkActor>)) {
      return;
    }
    if (actor instanceof descriptor.actorType) {
      return;
    }
    throw actorDispatchHandlerNotFound(
      `Actor handler '${descriptor.handlerType.name}' expects actor '${descriptor.actorType.name}'.`
    );
  }

  private createHandler<THandler>(descriptor: Pick<ZLinkActorPacketDescriptor, 'handlerType'>): THandler {
    const handler = this.options.handlerFactory?.(descriptor.handlerType)
      ?? this.options.providerResolver?.get?.(descriptor.handlerType)
      ?? new descriptor.handlerType();
    return handler as THandler;
  }

  private execute<T>(operation: () => Promise<T> | T): Promise<T> {
    return this.options.serial?.execute(operation) ?? Promise.resolve().then(operation);
  }

  private createSendContext(
    packetName: string,
    context: Partial<ZLinkSpotActorSendContext>
  ): ZLinkSpotActorSendContext {
    return {
      ...context,
      packetName,
      metadata: context.metadata ?? ZLinkMessageMetadataEmpty
    } as ZLinkSpotActorSendContext;
  }

  private createRequestContext(
    packetName: string,
    context: Partial<ZLinkSpotActorRequestContext>
  ): ZLinkSpotActorRequestContext {
    return {
      ...this.createSendContext(packetName, context),
      reply: context.reply ?? new DefaultZLinkSpotActorReplyOptions()
    } as ZLinkSpotActorRequestContext;
  }
}

function packetKey(kind: ZLinkActorPacketKind, actorType: Type<ZLinkActor>, packetName: string): string {
  return `${kind}:${actorType.name}:${packetName}`;
}

function actorDispatchHandlerNotFound(message: string): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound,
    message
  );
}
