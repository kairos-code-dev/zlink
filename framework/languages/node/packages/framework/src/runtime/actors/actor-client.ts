import {
  Message as BindingMessage,
  RequestResult,
  SubmitError,
  SubmitResult,
  RequestError,
  SendFlags
} from '@zlink-systems/zlink';
import type {
  ActorRef,
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
import { createAbortError, throwIfAborted } from '../abort';
import { encodeFrameworkPayloadMessage, decodeFrameworkPayloadMessage } from '../messaging/payload-codec';
import { resolveFrameworkPacketName } from '../messaging/packet-name';
import {
  decodeStreamHeader,
  encodeStreamHeader,
  tryDecodeStreamFrame,
  ZLinkStreamCodec,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind
} from '../streams/protocol';
import type { ZLinkStoreLocationResolvers } from '../locations';
import { captureZLinkSpotSerialTurn, type ZLinkSpotSerialTurn } from '../execution';

export interface ZLinkActorClientOptions {
  readonly nodeProvider: () => ZLinkBackendSpotNode | undefined;
  readonly locationResolver: () => ZLinkStoreLocationResolvers | undefined;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly defaultRequestTimeoutMs?: number;
  readonly staleActorRefReporter?: (actorId: string) => void;
  readonly sendErrorReporter?: (error: unknown) => void;
}

export class DefaultZLinkActorClient implements ZLinkActorClient {
  constructor(private readonly options: ZLinkActorClientOptions) {}

  sendToActor(actor: ActorRef, message: unknown): ZLinkActorSendCall {
    return new DefaultZLinkActorSendCall(
      (packetName, metadata) => this.send(actor, packetName, message, metadata),
      message,
      this.options.sendErrorReporter ?? (() => undefined)
    );
  }

  requestToActor(actor: ActorRef, request: unknown): ZLinkActorRequestCall {
    return new DefaultZLinkActorRequestCall(
      (packetName, timeoutMs, signal) => this.request(actor, packetName, request, timeoutMs, signal),
      request,
      this.options.defaultRequestTimeoutMs
    );
  }

  private async send(
    actor: ActorRef,
    explicitPacketName: string | undefined,
    message: unknown,
    metadata: ReadonlyMap<string, string>
  ): Promise<void> {
    const parts = this.createPacketParts(ZLinkStreamMessageKind.Send, undefined, explicitPacketName, message, metadata);
    try {
      await this.submitActorSend(actor as ZLinkBackendActorRef, parts);
    } catch (error) {
      if (isStaleActorError(error)) {
        this.options.staleActorRefReporter?.(actor.actorId);
        throw actorLocationStale(actor.actorId, error);
      }
      throw error;
    } finally {
      closeMessages(parts);
    }
  }

  private async request<TReply>(
    actor: ActorRef,
    explicitPacketName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    throwIfAborted(signal);
    const parts = this.createPacketParts(ZLinkStreamMessageKind.Request, 1n, explicitPacketName, request, new Map());
    try {
      return await this.submitActorRequest<TReply>(actor as ZLinkBackendActorRef, parts, timeoutMs, signal);
    } catch (error) {
      if (isStaleActorError(error)) {
        this.options.staleActorRefReporter?.(actor.actorId);
        throw actorLocationStale(actor.actorId, error);
      }
      throw error;
    } finally {
      closeMessages(parts);
    }
  }

  private createPacketParts(
    kind: ZLinkStreamMessageKind,
    requestSeq: bigint | undefined,
    explicitPacketName: string | undefined,
    message: unknown,
    metadata: ReadonlyMap<string, string>
  ): readonly Message[] {
    const packetName = resolveFrameworkPacketName(message, explicitPacketName, 'Actor');
    const header = encodeStreamHeader({
      kind,
      codec: ZLinkStreamCodec.Json,
      flags: requestSeq === undefined ? ZLinkStreamHeaderFlags.None : ZLinkStreamHeaderFlags.HasRequestSeq,
      requestSeq,
      name: packetName,
      metadata
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
              closeMessages(replyParts);
              reject(createAbortError());
              return;
            }
            if (result !== RequestResult.Ok) {
              closeMessages(replyParts);
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
  private readonly selectedMetadata = new Map<string, string>();
  private executed = false;

  constructor(
    private readonly submitter: (packetName: string | undefined, metadata: ReadonlyMap<string, string>) => Promise<void>,
    private readonly message: unknown,
    private readonly reportError: (error: unknown) => void
  ) {}

  packetName(packetName: string): this {
    this.packet = packetName;
    return this;
  }

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  submit(): void {
    ensureSingleSubmit(this.executed);
    this.executed = true;
    void this.submitter(
      this.packet ?? resolveFrameworkPacketName(this.message, undefined, 'Actor'),
      this.selectedMetadata
    ).catch(this.reportError);
  }
}

class DefaultZLinkActorRequestCall implements ZLinkActorRequestCall {
  private packet?: string;
  private timeoutMs?: number;
  private executed = false;
  private readonly turn: ZLinkSpotSerialTurn | undefined = captureZLinkSpotSerialTurn();

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

  metadata(_key: string, _value: string): this {
    return this;
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  submit<TReply>(signal?: AbortSignal): Promise<TReply> {
    return this.execute<TReply>(signal);
  }

  yield<TReply>(signal?: AbortSignal): Promise<TReply> {
    const pending = this.execute<TReply>(signal);
    return this.turn === undefined ? pending : this.turn.yieldPromise(pending);
  }

  private execute<TReply>(signal?: AbortSignal): Promise<TReply> {
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
  try {
    if (reply.length === 1) {
      const frame = tryDecodeStreamFrame(reply[0].data());
      if (frame !== undefined) {
        const header = decodeStreamHeader(frame.header);
        const payload = BindingMessage.from(frame.payload) as Message;
        try {
          return decodeActorReplyPayload<TReply>(header.kind, payload, serializers);
        } finally {
          payload.close();
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
  } finally {
    closeMessages(reply);
  }
}

function closeMessages(parts: readonly Message[]): void {
  for (const part of parts) part.close();
}

function decodeActorReplyPayload<TReply>(
  kind: ZLinkStreamMessageKind,
  payload: Message,
  serializers?: ReadonlyMap<string, ZLinkMessageSerializer>
): TReply {
  if (kind === ZLinkStreamMessageKind.Error) {
    const error = decodeFrameworkPayloadMessage<{
      readonly message?: string;
      readonly kind?: string;
      readonly isRetriable?: boolean;
    }>(payload, serializers);
    const kind = Object.values(ZLinkFrameworkErrorKind).includes(error.kind as ZLinkFrameworkErrorKind)
      ? error.kind as ZLinkFrameworkErrorKind
      : ZLinkFrameworkErrorKind.RequestFailed;
    throw new ZLinkFrameworkException(
      kind,
      error.message ?? 'Actor request failed.',
      error.isRetriable
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
    && error.kind === ZLinkFrameworkErrorKind.ActorLocationStale;
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
