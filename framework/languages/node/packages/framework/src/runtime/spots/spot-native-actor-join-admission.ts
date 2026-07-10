import type {
  ZLinkActor,
  ZLinkMessage,
  ZLinkMessageSerializer,
  ZLinkSpotActorJoinResponse
} from '../../contracts';
import {
  ZLinkDispatchErrorAction,
  ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { Message as BindingMessage } from '@zlink-systems/zlink';
import type {
  ZLinkBackendActorJoinInfo,
  ZLinkBackendActorJoinRequest,
  ZLinkBackendActorRef,
  ZLinkBackendSpot
} from '../backend/contracts';
import type { ZLinkDispatchErrorReporter } from '../channels';
import type { ZLinkRemoteBoundSessionTarget } from '../actors';
import {
  encodeFrameworkPayloadMessage,
  wrapFrameworkPayloadMessage
} from '../messaging/payload-codec';
import type { ZLinkSpotSerialExecutor } from './spot-serial-executor';
import {
  REMOTE_ACTOR_JOIN_PACKET,
  type ZLinkRemoteActorJoinActor
} from './spot-remote-codec';

interface ZLinkNativeActorJoinAdmissionTarget {
  onActorJoin?(actor: ZLinkActor, request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotActorJoinResponse>;
  onJoinedActor?(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
}

interface ZLinkSpotNativeActorJoinAdmissionOptions {
  readonly nativeSpot: ZLinkBackendSpot;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly resolveActor: (actorId: string) => ZLinkActor | undefined;
  readonly getTarget: () => ZLinkNativeActorJoinAdmissionTarget;
  readonly defaultAccept: boolean;
  readonly routedActorProvider?: (
    actorId: string,
    actorType: string,
    actorRef?: ZLinkBackendActorRef,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    actorCreateRequest?: Message,
    signal?: AbortSignal
  ) => Promise<ZLinkRemoteActorJoinActor>;
  readonly nativeJoinBoundSessionTargetResolver?: (
    info: ZLinkBackendActorJoinInfo
  ) => ZLinkRemoteBoundSessionTarget | undefined;
  readonly commitRoutedActor?: (actor: ZLinkActor) => Promise<void> | void;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
}

export class ZLinkSpotNativeActorJoinAdmission {
  constructor(private readonly options: ZLinkSpotNativeActorJoinAdmissionOptions) {}

  async admit(request: ZLinkBackendActorJoinRequest): Promise<void> {
    const actorId = request.info.targetActor.actorId;
    let accepted = false;
    let reply: Message | undefined;
    let joinedActor: ZLinkActor | undefined;
    const decoded = this.decodeNativeActorJoinRequest(request.message);
    try {
      let actor = this.options.resolveActor(actorId);
      if (actor === undefined && decoded !== undefined && this.options.routedActorProvider !== undefined) {
        const remoteBoundSessionTarget = this.options.nativeJoinBoundSessionTargetResolver?.(request.info);
        actor = (await this.options.routedActorProvider(
          actorId,
          decoded.actorType,
          request.info.targetActor,
          remoteBoundSessionTarget,
          decoded.actorCreateRequest
        )).actor;
      }
      if (actor !== undefined) {
        const target = this.options.getTarget();
        const joinRequest = decoded?.request ?? request.message;
        const joinPayload = wrapFrameworkPayloadMessage(joinRequest, this.options.messageSerializers);
        const response: ZLinkSpotActorJoinResponse = await this.options.serial.execute(async () =>
          target.onActorJoin === undefined
            ? { accepted: this.options.defaultAccept }
            : target.onActorJoin(actor, joinPayload)
        );
        accepted = response.accepted;
        reply = response.reply === undefined
          ? undefined
          : encodeFrameworkPayloadMessage(response.reply, this.options.messageSerializers);
        if (accepted) {
          this.options.commitRoutedActor?.(actor);
          joinedActor = actor;
        }
      }
    } catch (error) {
      this.reportHandlerException(actorId, error);
      accepted = false;
      reply = undefined;
    }
    if (joinedActor !== undefined) {
      try {
        await this.options.serial.execute(() => this.options.getTarget().onJoinedActor?.(joinedActor));
      } catch (error) {
        this.reportHandlerException(actorId, error);
      }
    }
    const operation = this.options.nativeSpot.replyActorJoin(request, accepted ? 0 : 1);
    let submitted = false;
    try {
      if (reply !== undefined) {
        operation.message(reply);
      }
      operation.submit();
      submitted = true;
    } finally {
      if (!submitted) {
        reply?.close();
      }
      decoded?.request.close();
      decoded?.actorCreateRequest?.close();
    }
  }

  private decodeNativeActorJoinRequest(message: Message): {
    readonly actorType: string;
    readonly actorCreateRequest?: Message;
    readonly request: Message;
  } | undefined {
    try {
      const payload = JSON.parse(message.data().toString()) as {
        readonly packetName?: unknown;
        readonly actorType?: unknown;
        readonly actorNodeRid?: unknown;
        readonly actorNodeRidHex?: unknown;
        readonly actorGeneration?: unknown;
        readonly actorCreateRequest?: unknown;
        readonly request?: unknown;
      };
      if (
        payload.packetName !== REMOTE_ACTOR_JOIN_PACKET ||
        typeof payload.actorType !== 'string' ||
        typeof payload.request !== 'string'
      ) {
        return undefined;
      }
      return {
        actorType: payload.actorType,
        actorCreateRequest: typeof payload.actorCreateRequest === 'string'
          ? BindingMessage.from(Buffer.from(payload.actorCreateRequest, 'base64'))
          : undefined,
        request: BindingMessage.from(Buffer.from(payload.request, 'base64'))
      };
    } catch {
      return undefined;
    }
  }

  private reportHandlerException(actorId: string, error: unknown): void {
    this.options.dispatchErrors?.report({
      surface: ZLinkDispatchErrorSurface.SpotActor,
      messageKind: ZLinkDispatchMessageKind.ActorSend,
      reason: ZLinkDispatchErrorReason.HandlerException,
      action: ZLinkDispatchErrorAction.Drop,
      actorId,
      error
    });
  }
}
