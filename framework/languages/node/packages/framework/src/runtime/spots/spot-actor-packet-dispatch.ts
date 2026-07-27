import type {
  ActorRef,
  ZLinkActor,
  ZLinkMessageSerializer,
  ZLinkSpot
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  zlinkMessageMetadata
} from '../../contracts';
import {
  ZLinkRuntimeMessageFlowOutcome as ZLinkMessageFlowOutcome,
  ZLinkRuntimeDispatchErrorAction as ZLinkDispatchErrorAction,
  ZLinkRuntimeDispatchErrorReason as ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind
} from '../../contracts/Dispatch/ZLinkDispatchOptions';
import { flowIfEnabled } from '../diagnostics';
import { createInboundFlow, runWithFlow } from '../diagnostics/flow-context';
import type { ZLinkRemoteBoundSessionTarget } from '../actors';
import {
  ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET,
  ZLinkSpotActorDispatcher,
  ZLinkSpotActorHandlerRegistryRuntime
} from '../actors';
import type { ZLinkDispatchErrorReporter } from '../channels';
import {
  decodeStreamHeader,
  messageToBytes,
  ZLinkStreamMessageKind
} from '../streams/protocol';
import { decodeFrameworkTypedPayloadMessage } from '../messaging/payload-codec';
import type { ZLinkProviderResolver } from '../../contracts/Common/ZLinkProviderResolver';
import type { ZLinkSpotSerialExecutor } from './spot-serial-executor';

export interface ZLinkActorResponseOptions {
  readonly metadata: ReadonlyMap<string, string>;
  readonly compressPayload: boolean;
}

interface ZLinkSpotActorPacketDispatchOptions {
  readonly spot: ZLinkSpot;
  readonly spotId: () => string;
  readonly registry: ZLinkSpotActorHandlerRegistryRuntime;
  readonly serial?: ZLinkSpotSerialExecutor;
  readonly resolveActor: (actorId: string) => ZLinkActor | undefined;
  readonly actorLeft?: (actorId: string) => boolean;
  readonly routeBeforeLocal?: (
    actorId: string,
    parts: readonly Message[],
    returnResponse: boolean,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ) => Promise<{ readonly handled: boolean; readonly response?: unknown } | undefined> |
    { readonly handled: boolean; readonly response?: unknown } |
    undefined;
  readonly onRemoteBoundSessionTarget?: (
    actorId: string,
    target: ZLinkRemoteBoundSessionTarget | undefined
  ) => void;
  readonly onDisconnectActor: (actor: ZLinkActor) => Promise<void>;
  readonly actorResponseSender?: (
    actor: ZLinkActor,
    packetName: string,
    requestSeq: bigint,
    response: unknown,
    replyOptions: ZLinkActorResponseOptions,
    fallbackBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef,
    signal?: AbortSignal
  ) => Promise<void> | void;
  readonly actorErrorSender?: (
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    fallbackBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ) => Promise<void> | void;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
}

export class ZLinkSpotActorPacketDispatch {
  constructor(private readonly options: ZLinkSpotActorPacketDispatchOptions) {}

  async dispatch(
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    if (parts.length < 2) {
      this.reportInvalidFrame(actorId, ZLinkDispatchMessageKind.ActorSend);
      return undefined;
    }
    let header: ReturnType<typeof decodeStreamHeader>;
    try {
      header = decodeStreamHeader(messageToBytes(parts[0]));
    } catch (error) {
      this.reportInvalidFrame(actorId, ZLinkDispatchMessageKind.ActorSend, error);
      throw error;
    }
    return await runWithFlow(createInboundFlow(
      header.flowId,
      header.flowOrigin,
      this.options.dispatchErrors?.flow.flowCreationEnabled() ?? true
    ), async () => {
      const messageKind = header.kind === ZLinkStreamMessageKind.Request
        ? ZLinkDispatchMessageKind.ActorRequest
        : ZLinkDispatchMessageKind.ActorSend;
      const action = messageKind === ZLinkDispatchMessageKind.ActorRequest
        ? ZLinkDispatchErrorAction.ReplyError
        : ZLinkDispatchErrorAction.Drop;
      this.trace(ZLinkMessageFlowOutcome.Received, actorId, header, messageKind);
      if (
        this.options.actorLeft?.(actorId) === true &&
        header.name === ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET
      ) {
        return undefined;
      }
      if (remoteBoundSessionTarget !== undefined) {
        this.options.onRemoteBoundSessionTarget?.(actorId, remoteBoundSessionTarget);
      }
      const routed = await this.options.routeBeforeLocal?.(
        actorId,
        parts,
        returnResponse,
        remoteBoundSessionTarget,
        fallbackActorRef
      );
      if (routed?.handled === true) {
        return routed.response;
      }
      const actor = this.options.resolveActor(actorId);
      if (actor === undefined) {
        return this.handleMissingActor(
          actorId,
          header,
          messageKind,
          action,
          returnResponse,
          remoteBoundSessionTarget,
          fallbackActorRef
        );
      }
      if (header.name === ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET) {
        this.options.onRemoteBoundSessionTarget?.(actorId, undefined);
        await this.options.onDisconnectActor(actor);
        return undefined;
      }
      const payload = this.decodePayload(actorId, parts[1], header, messageKind, action);
      return this.dispatchDecodedActorPacket(
        actor,
        actorId,
        payload,
        header,
        messageKind,
        action,
        returnResponse,
        remoteBoundSessionTarget,
        fallbackActorRef
      );
    });
  }

  private async handleMissingActor(
    actorId: string,
    header: ReturnType<typeof decodeStreamHeader>,
    messageKind: ZLinkDispatchMessageKind,
    action: ZLinkDispatchErrorAction,
    returnResponse: boolean,
    fallbackBoundSessionTarget: ZLinkRemoteBoundSessionTarget | undefined,
    fallbackActorRef: ActorRef | undefined
  ): Promise<undefined> {
    this.options.dispatchErrors?.report({
      surface: ZLinkDispatchErrorSurface.SpotActor,
      messageKind,
      reason: ZLinkDispatchErrorReason.HandlerMissing,
      action,
      packetName: header.name,
      spotId: this.options.spotId(),
      actorId,
      correlationId: header.correlationId ?? header.requestSeq?.toString()
    });
    if (messageKind !== ZLinkDispatchMessageKind.ActorRequest) {
      return undefined;
    }
    const missingActorError = new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound,
      `SPOT actor is not registered locally: ${actorId}`
    );
    if (header.requestSeq !== undefined && !returnResponse && this.options.actorErrorSender !== undefined) {
      await this.options.actorErrorSender(
        actorId,
        header.name,
        header.requestSeq,
        missingActorError,
        header.metadata,
        fallbackBoundSessionTarget,
        fallbackActorRef
      );
      return undefined;
    }
    throw missingActorError;
  }

  private decodePayload(
    actorId: string,
    message: Message,
    header: ReturnType<typeof decodeStreamHeader>,
    messageKind: ZLinkDispatchMessageKind,
    action: ZLinkDispatchErrorAction
  ): unknown {
    try {
      return decodeFrameworkTypedPayloadMessage(message, this.options.messageSerializers);
    } catch (error) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotActor,
        messageKind,
        reason: ZLinkDispatchErrorReason.PayloadDecodeFailed,
        action,
        packetName: header.name,
        spotId: this.options.spotId(),
        actorId,
        correlationId: header.correlationId ?? header.requestSeq?.toString(),
        error
      });
      throw error;
    }
  }

  private async dispatchDecodedActorPacket(
    actor: ZLinkActor,
    actorId: string,
    payload: unknown,
    header: ReturnType<typeof decodeStreamHeader>,
    messageKind: ZLinkDispatchMessageKind,
    action: ZLinkDispatchErrorAction,
    returnResponse: boolean,
    fallbackBoundSessionTarget: ZLinkRemoteBoundSessionTarget | undefined,
    fallbackActorRef: ActorRef | undefined
  ): Promise<unknown> {
    const dispatcher = new ZLinkSpotActorDispatcher({
      registry: this.options.registry,
      spot: this.options.spot,
      providerResolver: this.options.providerResolver,
      serial: this.options.serial,
      messageSerializers: this.options.messageSerializers
    });
    try {
      if (header.kind === ZLinkStreamMessageKind.Send) {
        await dispatcher.dispatchSend(actor, header.name, payload, {
          meshName: this.options.spot.context.meshName,
          metadata: zlinkMessageMetadata(header.metadata),
          correlationId: header.correlationId ?? undefined
        });
        this.trace(ZLinkMessageFlowOutcome.Dispatched, actorId, header, ZLinkDispatchMessageKind.ActorSend);
        return undefined;
      }
      if (header.kind !== ZLinkStreamMessageKind.Request || header.requestSeq === undefined) {
        this.options.dispatchErrors?.report({
          surface: ZLinkDispatchErrorSurface.SpotActor,
          messageKind: ZLinkDispatchMessageKind.ActorRequest,
          reason: ZLinkDispatchErrorReason.InvalidFrame,
          action: ZLinkDispatchErrorAction.Drop,
          packetName: header.name,
          spotId: this.options.spotId(),
          actorId
        });
        return undefined;
      }
      const requestSeq = header.requestSeq;
      if (returnResponse || this.options.actorResponseSender === undefined) {
        const response = await dispatcher.dispatchRequest(actor, header.name, payload, {
          meshName: this.options.spot.context.meshName,
          metadata: zlinkMessageMetadata(header.metadata),
          correlationId: header.correlationId ?? header.requestSeq.toString()
        });
        this.trace(ZLinkMessageFlowOutcome.Replied, actorId, header, ZLinkDispatchMessageKind.ActorRequest);
        return response;
      }
      await dispatcher.dispatchRequestThen(actor, header.name, payload, {
        meshName: this.options.spot.context.meshName,
        metadata: zlinkMessageMetadata(header.metadata),
        correlationId: header.correlationId ?? header.requestSeq.toString()
      }, async (response, replyOptions) => {
        this.trace(ZLinkMessageFlowOutcome.Replied, actorId, header, ZLinkDispatchMessageKind.ActorRequest);
        await this.options.actorResponseSender?.(
          actor,
          header.name,
          requestSeq,
          response,
          replyOptions,
          fallbackBoundSessionTarget,
          fallbackActorRef,
          undefined
        );
      });
      return undefined;
    } catch (error) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotActor,
        messageKind,
        reason: error instanceof ZLinkFrameworkException
          && error.kind === ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound
          ? ZLinkDispatchErrorReason.HandlerMissing
          : ZLinkDispatchErrorReason.HandlerException,
        action,
        packetName: header.name,
        spotId: this.options.spotId(),
        actorId,
        correlationId: header.correlationId ?? header.requestSeq?.toString(),
        error
      });
      if (
        messageKind === ZLinkDispatchMessageKind.ActorRequest &&
        header.requestSeq !== undefined &&
        !returnResponse &&
        this.options.actorErrorSender !== undefined
      ) {
        await this.options.actorErrorSender(
          actorId,
          header.name,
          header.requestSeq,
          error,
          header.metadata,
          fallbackBoundSessionTarget,
          fallbackActorRef
        );
        return undefined;
      }
      throw error;
    }
  }

  private reportInvalidFrame(
    actorId: string,
    messageKind: ZLinkDispatchMessageKind,
    error?: unknown
  ): void {
    this.options.dispatchErrors?.report({
      surface: ZLinkDispatchErrorSurface.SpotActor,
      messageKind,
      reason: ZLinkDispatchErrorReason.InvalidFrame,
      action: ZLinkDispatchErrorAction.Drop,
      spotId: this.options.spotId(),
      actorId,
      error
    });
  }

  private trace(
    outcome: ZLinkMessageFlowOutcome,
    actorId: string,
    header: ReturnType<typeof decodeStreamHeader>,
    messageKind: ZLinkDispatchMessageKind
  ): void {
    flowIfEnabled(this.options.dispatchErrors?.flow, outcome)?.trace({
      outcome,
      surface: ZLinkDispatchErrorSurface.SpotActor,
      messageKind,
      packetName: header.name,
      spotId: this.options.spotId(),
      actorId,
      correlationId: header.correlationId ?? header.requestSeq?.toString()
    });
  }
}
