import {
  Message as BindingMessage,
  RequestResult,
  SubmitError,
  SubmitResult,
  RequestError,
  SendFlags
} from '@zlink-systems/zlink';
import type {
  ZLinkActorClient,
  ZLinkActorRequestCall,
  ZLinkActorSendCall,
  ZLinkMessageSerializer
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendActorRef, ZLinkBackendSpotNode } from '../backend';
import { encodeFrameworkPayloadMessage, decodeFrameworkPayloadMessage } from '../messaging/payload-codec';
import { resolveFrameworkPacketName } from '../messaging/packet-name';
import {
  decodeStreamHeader,
  encodeStreamHeader,
  ZLinkStreamCodec,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind
} from '../streams/protocol';
import type { ZLinkStoreLocationResolvers } from '../locations';

export interface ZLinkActorClientOptions {
  readonly nodeProvider: () => ZLinkBackendSpotNode | undefined;
  readonly locationResolver: () => ZLinkStoreLocationResolvers | undefined;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly defaultRequestTimeoutMs?: number;
}

export class DefaultZLinkActorClient implements ZLinkActorClient {
  constructor(private readonly options: ZLinkActorClientOptions) {}

  sendToActor(actorId: string, message: unknown): ZLinkActorSendCall {
    return new DefaultZLinkActorSendCall(
      (packetName, signal) => this.send(actorId, packetName, message, signal),
      message
    );
  }

  requestToActor(actorId: string, request: unknown): ZLinkActorRequestCall {
    return new DefaultZLinkActorRequestCall(
      (packetName, timeoutMs, signal) => this.request(actorId, packetName, request, timeoutMs, signal),
      request,
      this.options.defaultRequestTimeoutMs
    );
  }

  private async send(
    actorId: string,
    explicitPacketName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    let actor = await this.resolveActor(actorId, signal, ZLinkFrameworkErrorKind.ActorRouteNotFound);
    let parts = this.createPacketParts(ZLinkStreamMessageKind.Send, undefined, explicitPacketName, message);
    try {
      await this.submitActorSend(actor, parts);
    } catch (error) {
      if (!isStaleActorError(error)) {
        throw error;
      }
      actor = await this.resolveActor(actorId, signal, ZLinkFrameworkErrorKind.ActorLocationStale);
      parts = this.createPacketParts(ZLinkStreamMessageKind.Send, undefined, explicitPacketName, message);
      try {
        await this.submitActorSend(actor, parts);
      } catch (retryError) {
        if (isStaleActorError(retryError)) {
          throw actorLocationStale(actorId, retryError);
        }
        throw retryError;
      }
    }
  }

  private async request<TReply>(
    actorId: string,
    explicitPacketName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    throwIfAborted(signal);
    let actor = await this.resolveActor(actorId, signal, ZLinkFrameworkErrorKind.ActorRouteNotFound);
    let parts = this.createPacketParts(ZLinkStreamMessageKind.Request, 1n, explicitPacketName, request);
    try {
      return await this.submitActorRequest<TReply>(actor, parts, timeoutMs, signal);
    } catch (error) {
      if (!isStaleActorError(error)) {
        throw error;
      }
      actor = await this.resolveActor(actorId, signal, ZLinkFrameworkErrorKind.ActorLocationStale);
      parts = this.createPacketParts(ZLinkStreamMessageKind.Request, 1n, explicitPacketName, request);
      try {
        return await this.submitActorRequest<TReply>(actor, parts, timeoutMs, signal);
      } catch (retryError) {
        if (isStaleActorError(retryError)) {
          throw actorLocationStale(actorId, retryError);
        }
        throw retryError;
      }
    }
  }

  private async resolveActor(
    actorId: string,
    signal: AbortSignal | undefined,
    missingKind: ZLinkFrameworkErrorKind.ActorRouteNotFound | ZLinkFrameworkErrorKind.ActorLocationStale
  ): Promise<ZLinkBackendActorRef> {
    const resolver = this.options.locationResolver();
    if (resolver === undefined) {
      throw new ZLinkFrameworkException(
        missingKind,
        `Actor route '${actorId}' was not found.`
      );
    }
    const row = await resolver.resolveActorRow({ actorId }, signal);
    if (row?.actorRef !== undefined) {
      return row.actorRef as ZLinkBackendActorRef;
    }
    throw new ZLinkFrameworkException(
      missingKind,
      missingKind === ZLinkFrameworkErrorKind.ActorLocationStale
        ? `Actor route '${actorId}' became stale.`
        : `Actor route '${actorId}' was not found.`
    );
  }

  private createPacketParts(
    kind: ZLinkStreamMessageKind,
    requestSeq: bigint | undefined,
    explicitPacketName: string | undefined,
    message: unknown
  ): readonly Message[] {
    const packetName = resolveFrameworkPacketName(message, explicitPacketName, 'Actor');
    const header = encodeStreamHeader({
      kind,
      codec: ZLinkStreamCodec.Json,
      flags: requestSeq === undefined ? ZLinkStreamHeaderFlags.None : ZLinkStreamHeaderFlags.HasRequestSeq,
      requestSeq,
      name: packetName,
      metadata: new Map()
    });
    return [
      BindingMessage.from(Buffer.from(header)) as Message,
      encodeFrameworkPayloadMessage(message, this.options.messageSerializers)
    ];
  }

  private async submitActorSend(actor: ZLinkBackendActorRef, parts: readonly Message[]): Promise<void> {
    const node = this.requireNode();
    try {
      if (!await node.sendToActor(actor, parts, SendFlags.None)) {
        throw routeNotConnected('Actor send submit was not accepted.');
      }
    } catch (error) {
      throw error instanceof RequestError
        ? mapRequestError(error, 'Actor send')
        : mapSubmitError(error, 'Actor send');
    }
  }

  private async submitActorRequest<TReply>(
    actor: ZLinkBackendActorRef,
    parts: readonly Message[],
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    const node = this.requireNode();
    const reply = await new Promise<readonly Message[]>((resolve, reject) => {
      try {
        const accepted = node.requestToActor(
          actor,
          parts,
          (result, replyParts) => {
            if (signal?.aborted === true) {
              reject(new Error('The operation was aborted.'));
              return;
            }
            if (result !== RequestResult.Ok) {
              reject(mapRequestResult(result, 'Actor request'));
              return;
            }
            resolve(replyParts);
          },
          SendFlags.None,
          timeoutMs
        );
        if (!accepted) {
          reject(routeNotConnected('Actor request submit was not accepted.'));
        }
      } catch (error) {
        reject(error instanceof RequestError
          ? mapRequestError(error, 'Actor request')
          : mapSubmitError(error, 'Actor request'));
      }
    });
    return decodeActorReply<TReply>(reply, this.options.messageSerializers);
  }

  private requireNode(): ZLinkBackendSpotNode {
    const node = this.options.nodeProvider();
    if (node === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RouteNotConnected,
        'Actor client requires a configured SPOT node.',
        true
      );
    }
    return node;
  }
}

class DefaultZLinkActorSendCall implements ZLinkActorSendCall {
  private packet?: string;
  private executed = false;

  constructor(
    private readonly submitter: (packetName: string | undefined, signal?: AbortSignal) => Promise<void>,
    private readonly message: unknown
  ) {}

  packetName(packetName: string): this {
    this.packet = packetName;
    return this;
  }

  submit(signal?: AbortSignal): Promise<void> {
    ensureSingleSubmit(this.executed);
    this.executed = true;
    throwIfAborted(signal);
    return this.submitter(this.packet ?? resolveFrameworkPacketName(this.message, undefined, 'Actor'), signal);
  }
}

class DefaultZLinkActorRequestCall implements ZLinkActorRequestCall {
  private packet?: string;
  private timeoutMs?: number;
  private executed = false;

  constructor(
    private readonly submitter: <TReply>(
      packetName: string | undefined,
      timeoutMs: number | undefined,
      signal?: AbortSignal
    ) => Promise<TReply>,
    private readonly request: unknown,
    private readonly defaultRequestTimeoutMs?: number
  ) {}

  packetName(packetName: string): this {
    this.packet = packetName;
    return this;
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  submit<TReply>(signal?: AbortSignal): Promise<TReply> {
    ensureSingleSubmit(this.executed);
    this.executed = true;
    throwIfAborted(signal);
    return this.submitter<TReply>(
      this.packet ?? resolveFrameworkPacketName(this.request, undefined, 'Actor'),
      this.timeoutMs ?? this.defaultRequestTimeoutMs,
      signal
    );
  }
}

function decodeActorReply<TReply>(
  reply: readonly Message[],
  serializers?: ReadonlyMap<string, ZLinkMessageSerializer>
): TReply {
  if (reply.length === 1) {
    const frame = reply[0].data();
    if (frame.length >= 6) {
      const headerSize = frame.readUInt16BE(0);
      const payloadSize = frame.readUInt32BE(2);
      if (frame.length === 6 + headerSize + payloadSize) {
        const header = decodeStreamHeader(frame.subarray(6, 6 + headerSize));
        return decodeActorReplyPayload<TReply>(
          header.kind,
          BindingMessage.from(frame.subarray(6 + headerSize, 6 + headerSize + payloadSize)) as Message,
          serializers
        );
      }
    }
  }
  if (reply.length >= 2) {
    const header = decodeStreamHeader(reply[0].data());
    return decodeActorReplyPayload<TReply>(header.kind, reply[1], serializers);
  }
  throw new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.RequestProtocolError,
    'Actor request reply is empty.'
  );
}

function decodeActorReplyPayload<TReply>(
  kind: ZLinkStreamMessageKind,
  payload: Message,
  serializers?: ReadonlyMap<string, ZLinkMessageSerializer>
): TReply {
  if (kind === ZLinkStreamMessageKind.Error) {
    const error = decodeFrameworkPayloadMessage<{ readonly message?: string }>(payload, serializers);
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.RequestFailed,
      error.message ?? 'Actor request failed.'
    );
  }
  return decodeFrameworkPayloadMessage<TReply>(payload, serializers);
}

function mapSubmitError(error: unknown, operationName: string): Error {
  if (error instanceof ZLinkFrameworkException) {
    return error;
  }
  if (error instanceof SubmitError) {
    switch (error.result) {
      case SubmitResult.NotConnected:
        return routeNotConnected(`${operationName} failed because the target route is not connected.`, error);
      case SubmitResult.NotFound:
        return new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `${operationName} failed because the actor route was not found.`,
          false,
          error
        );
      default:
        return new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.RequestFailed,
          `${operationName} failed with submit result ${error.result}.`,
          false,
          error
        );
    }
  }
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.RouteNotConnected,
    `${operationName} failed before a reply was received.`,
    true,
    error
  );
}

function mapRequestResult(result: number, operationName: string): Error {
  switch (result) {
    case RequestResult.NotConnected:
      return routeNotConnected(`${operationName} failed because the target route is not connected.`);
    case RequestResult.NotFound:
      return new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `${operationName} failed because the actor route was not found.`
      );
    case RequestResult.Conflict:
      return new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorLocationStale,
        `${operationName} failed because the actor location is stale.`
      );
    default:
      return new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestFailed,
        `${operationName} failed with request result ${result}.`
      );
  }
}

function mapRequestError(error: RequestError, operationName: string): Error {
  return mapRequestResult(error.result, operationName);
}

function routeNotConnected(message: string, cause?: unknown): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.RouteNotConnected,
    message,
    true,
    cause
  );
}

function isStaleActorError(error: unknown): boolean {
  return error instanceof ZLinkFrameworkException
    && (
      error.kind === ZLinkFrameworkErrorKind.ActorRouteNotFound
      || error.kind === ZLinkFrameworkErrorKind.ActorLocationStale
    );
}

function actorLocationStale(actorId: string, cause: unknown): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.ActorLocationStale,
    `Actor route '${actorId}' is stale after re-resolve.`,
    true,
    cause
  );
}

function ensureSingleSubmit(executed: boolean): void {
  if (executed) {
    throw new Error('Actor client calls can be submitted only once.');
  }
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted === true) {
    throw new Error('The operation was aborted.');
  }
}
