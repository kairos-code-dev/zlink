import { Message as ZLinkBindingMessage, SubmitError, SubmitResult } from '@zlink-systems/zlink';
import type { ActorRef } from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { throwIfAborted } from '../abort';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import {
  encodeStreamHeader,
  resolvePacketName,
  ZLinkStreamCodec,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind
} from './protocol';
import {
  ZLinkActorSessionBindingRegistry,
  type ZLinkActorSessionRoute
} from './actor-session-binding-registry';
import type {
  DefaultZLinkSessionActor,
  DefaultZLinkSessionContext
} from './session-context';
import {
  boundSessionErrorPayload
} from './bound-session-response-target';
import {
  ZLinkStreamFrameMessageFactory
} from './stream-frame-factory';
import {
  ZLinkManagedStream
} from './managed-stream';

const ZLINK_NATIVE_BOUND_SESSION_RETRY_DELAY_MS = 10;
const ZLINK_SEND_DONT_WAIT = 1;

type ZLinkStreamActorSessionRoute = ZLinkActorSessionRoute<DefaultZLinkSessionContext, DefaultZLinkSessionActor>;

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

export interface ZLinkBoundSessionServiceOptions {
  readonly transport?: ZLinkBoundSessionTransport;
  readonly actorBindTimeoutMs?: number;
}

export class ZLinkBoundSessionService {
  constructor(
    private readonly routes: ZLinkActorSessionBindingRegistry<DefaultZLinkSessionContext, DefaultZLinkSessionActor>,
    private readonly frameMessages: ZLinkStreamFrameMessageFactory,
    private readonly options: ZLinkBoundSessionServiceOptions = {}
  ) {}

  async sendBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const route = this.routes.requireRoute(actorId);
    const frame = this.frameMessages.createJsonFrameMessage(
      ZLinkStreamMessageKind.Send,
      resolvePacketName(message, packetName),
      metadata,
      false,
      undefined,
      message
    );
    try {
      await this.requireTransport().send(actorId, frame, {
        bindingToken: route.bindingToken,
        packetName,
        metadata,
        signal
      });
      this.routes.requireCurrentToken(actorId, route.bindingToken);
    } finally {
      frame.close();
    }
  }

  sendLocalBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>
  ): boolean {
    const route = this.routes.route(actorId);
    if (route === undefined) {
      return false;
    }
    return this.writeLocalBoundSessionFrame(
      actorId,
      route,
      ZLinkStreamMessageKind.Send,
      resolvePacketName(message, packetName),
      metadata,
      false,
      undefined,
      message,
      'send'
    );
  }

  sendLocalBoundSessionResponse(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    metadata: ReadonlyMap<string, string>,
    compressPayload: boolean
  ): boolean {
    const route = this.routes.route(actorId);
    if (route === undefined) {
      return false;
    }
    return this.writeLocalBoundSessionFrame(
      actorId,
      route,
      ZLinkStreamMessageKind.Response,
      packetName,
      metadata,
      compressPayload,
      requestSeq,
      message,
      'response'
    );
  }

  sendLocalBoundSessionError(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>
  ): boolean {
    const route = this.routes.route(actorId);
    if (route === undefined) {
      return false;
    }
    return this.writeLocalBoundSessionFrame(
      actorId,
      route,
      ZLinkStreamMessageKind.Error,
      packetName,
      metadata,
      false,
      requestSeq,
      boundSessionErrorPayload(error),
      'error response'
    );
  }

  async sendNativeBoundSession(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    await this.sendNativeBoundSessionPayload(
      node,
      actorRef,
      ZLinkStreamMessageKind.Send,
      resolvePacketName(message, packetName),
      metadata,
      false,
      undefined,
      message,
      signal
    );
  }

  async sendNativeBoundSessionResponse(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    metadata: ReadonlyMap<string, string>,
    compressPayload: boolean,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    await this.sendNativeBoundSessionPayload(
      node,
      actorRef,
      ZLinkStreamMessageKind.Response,
      packetName,
      metadata,
      compressPayload,
      requestSeq,
      message,
      signal
    );
  }

  async sendNativeBoundSessionError(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    await this.sendNativeBoundSessionPayload(
      node,
      actorRef,
      ZLinkStreamMessageKind.Error,
      packetName,
      metadata,
      false,
      requestSeq,
      boundSessionErrorPayload(error),
      signal
    );
  }

  async disconnectNativeBoundSession(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    await node.closeActorBoundSession(actorRef as unknown as ZLinkBackendActorRef, 0, signal);
  }

  async disconnectBoundSession(actorId: string, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const route = this.routes.requireRoute(actorId);
    try {
      await this.requireTransport().disconnect(actorId, {
        bindingToken: route.bindingToken,
        signal
      });
    } finally {
      this.routes.unbind(actorId, route.context, route.bindingToken);
    }
  }

  relayRemoteBoundSessionBind(stream: ZLinkManagedStream, actorRef: ActorRef): void {
    const header = ZLinkBindingMessage.from(Buffer.from(encodeStreamHeader({
      kind: ZLinkStreamMessageKind.Send,
      codec: ZLinkStreamCodec.Raw,
      flags: ZLinkStreamHeaderFlags.None,
      name: 'zlink.framework.actor.bound_session.bind',
      metadata: new Map()
    })));
    const body = ZLinkBindingMessage.from(Buffer.alloc(0));
    try {
      if (!stream.sendBoundActor(actorRef.actorId, [header, body], 0)) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Actor '${actorRef.actorId}' remote bound session bind relay failed.`
        );
      }
    } finally {
      header.close();
      body.close();
    }
  }

  private writeLocalBoundSessionFrame(
    actorId: string,
    route: ZLinkStreamActorSessionRoute,
    kind: ZLinkStreamMessageKind,
    packetName: string,
    metadata: ReadonlyMap<string, string>,
    compressPayload: boolean,
    requestSeq: bigint | undefined,
    payload: unknown,
    operationName: string
  ): boolean {
    const frame = this.frameMessages.createJsonFrameMessage(
      kind,
      packetName,
      metadata,
      compressPayload,
      requestSeq,
      payload
    );
    try {
      if (this.routes.route(actorId)?.bindingToken !== route.bindingToken) {
        return false;
      }
      if (!route.context.stream.writeRaw(frame)) {
        throw new Error(`Actor '${actorId}' local bound session ${operationName} failed.`);
      }
      return true;
    } finally {
      frame.close();
    }
  }

  private async sendNativeBoundSessionPayload(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    kind: ZLinkStreamMessageKind,
    packetName: string,
    metadata: ReadonlyMap<string, string>,
    compressPayload: boolean,
    requestSeq: bigint | undefined,
    payload: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    const frame = this.frameMessages.createJsonFrameMessage(
      kind,
      packetName,
      metadata,
      compressPayload,
      requestSeq,
      payload
    );
    try {
      await this.sendNativeBoundSessionFrame(node, actorRef, frame, signal);
    } finally {
      frame.close();
    }
  }

  private async sendNativeBoundSessionFrame(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    frame: Message,
    signal?: AbortSignal
  ): Promise<void> {
    const deadline = Date.now() + (this.options.actorBindTimeoutMs ?? 2000);
    const backendActorRef = toBoundSessionSendActorRef(actorRef);
    let lastError: unknown;
    do {
      throwIfAborted(signal);
      try {
        if (node.sendActorBoundSession(backendActorRef, [frame], ZLINK_SEND_DONT_WAIT)) {
          return;
        }
        lastError = undefined;
      } catch (error) {
        if (!isNativeBoundSessionSendRetryable(error)) {
          throw error;
        }
        lastError = error;
      }
      await delayNativeBoundSessionRetry(deadline, signal);
    } while (Date.now() <= deadline);

    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorRouteNotFound,
      `Actor '${actorRef.actorId}' bound session route is not ready.`,
      false,
      lastError
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

function toBoundSessionSendActorRef(actor: ActorRef): ZLinkBackendActorRef {
  return {
    nodeRid: actor.nodeRid,
    actorId: actor.actorId,
    generation: 0n
  };
}


async function delayNativeBoundSessionRetry(deadline: number, signal: AbortSignal | undefined): Promise<void> {
  const remaining = deadline - Date.now();
  if (remaining <= 0) {
    return;
  }
  await new Promise<void>((resolve) =>
    setTimeout(resolve, Math.min(ZLINK_NATIVE_BOUND_SESSION_RETRY_DELAY_MS, remaining))
  );
  throwIfAborted(signal);
}

function isNativeBoundSessionSendRetryable(error: unknown): boolean {
  return error instanceof SubmitError &&
    (error.result === SubmitResult.Backpressured || error.result === SubmitResult.NotConnected);
}
