import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkMessageSerializer,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse
} from '../../contracts';
import {
  ZLinkDispatchErrorAction,
  ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind,
  ZLinkEncodedPayload,
  ZLinkMessage
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import {
  Message as BindingMessage
} from '@zlink-systems/zlink';
import {
  ZLinkConfigurationException
} from '../configuration';
import { ZLinkDispatchErrorReporter } from '../channels';
import { ZLinkSpotActorDispatcher } from '../actors';
import {
  encodeFrameworkPayloadMessage
} from '../messaging/payload-codec';
import type { ZLinkSpotActivation } from './spot-activation-registry';

export interface ZLinkSpotActorMembershipOptions {
  readonly resolveActivation: (spotRid: RoutingId) => ZLinkSpotActivation | undefined;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly entrySpotCallbacks?: {
    onLeaveActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
  };
  readonly nodeRid?: RoutingId;
  readonly entryNodeRid?: RoutingId;
  readonly entryNodeRidProvider?: () => RoutingId | undefined;
  readonly actorEntryNodeRidProvider?: (actor: ZLinkActor) => RoutingId | undefined;
  readonly routedActorLeaveCommitter?: (actor: ZLinkActor) => void;
}

export class ZLinkSpotActorMembership {
  constructor(private readonly options: ZLinkSpotActorMembershipOptions) {}

  async admitActorJoin(
    spotRid: RoutingId,
    actor: ZLinkActor,
    request: Message,
    commit: (spot: ZLinkSpot) => Promise<void> | void,
    signal?: AbortSignal
  ): Promise<ZLinkSpotActorJoinResponse> {
    throwIfAborted(signal);
    const activation = this.requireActivation(spotRid);
    const dispatcher = this.createActorDispatcher(activation);
    const response = await dispatcher.admitActorJoin(actor, request, async () => {
      await commit(activation.spot);
      activation.actors.set(actor.actorId, actor);
      const entryLeave = this.options.entrySpotCallbacks?.onLeaveActor(actor, signal);
      if (entryLeave !== undefined) {
        void entryLeave.catch((error) => {
          this.options.dispatchErrors?.report({
            surface: ZLinkDispatchErrorSurface.SpotActor,
            messageKind: ZLinkDispatchMessageKind.ActorSend,
            reason: ZLinkDispatchErrorReason.HandlerException,
            action: ZLinkDispatchErrorAction.Drop,
            spotRid: String(spotRid),
            actorId: actor.actorId,
            error
          });
        });
      }
    });
    return {
      accepted: response.accepted,
      reply: response.reply === undefined
        ? undefined
        : encodeFrameworkPayloadMessage(response.reply, this.options.messageSerializers)
    };
  }

  async leaveActor(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const activation = this.requireActivation(spotRid);
    await activation.serial.execute(async () => {
      activation.leftActors.add(actor.actorId);
      await activation.spot.onLeaveActor?.(actor, signal);
      activation.actors.delete(actor.actorId);
      this.options.routedActorLeaveCommitter?.(actor);
    });
    const localEntryNodeRid =
      this.options.entryNodeRidProvider?.() ??
      this.options.entryNodeRid ??
      this.options.nodeRid;
    const entryNodeRid = this.options.actorEntryNodeRidProvider?.(actor) ??
      localEntryNodeRid;
    if (entryNodeRid === undefined) {
      throw new ZLinkConfigurationException('Spot actor leave requires an Entry Spot node routing id.');
    }
    const actorRef = (actor.context as unknown as { actorRef?: ActorRef }).actorRef;
    if (
      localEntryNodeRid !== undefined &&
      actorRef?.nodeRid !== undefined &&
      String(actorRef.nodeRid) !== String(localEntryNodeRid) &&
      String(entryNodeRid) !== String(localEntryNodeRid)
    ) {
      return;
    }
    const request = BindingMessage.from(Buffer.alloc(0));
    try {
      await actor.context.joinEntrySpot(
        entryNodeRid,
        ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(request.data()))
      ).yield(signal);
    } finally {
      request.close();
    }
  }

  async notifyJoinedActorDisconnected(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<boolean> {
    throwIfAborted(signal);
    const activation = this.options.resolveActivation(spotRid);
    if (activation === undefined) {
      return false;
    }
    const joinedActor = activation.actors.get(actor.actorId);
    if (joinedActor === undefined) {
      return false;
    }
    await activation.serial.execute(() => activation.spot.onDisconnectActor?.(joinedActor, signal));
    return true;
  }

  private requireActivation(spotRid: RoutingId): ZLinkSpotActivation {
    const activation = this.options.resolveActivation(spotRid);
    if (activation === undefined) {
      throw new ZLinkConfigurationException(`Spot '${spotRid}' is not active.`);
    }
    return activation;
  }

  private createActorDispatcher(activation: ZLinkSpotActivation): ZLinkSpotActorDispatcher {
    return new ZLinkSpotActorDispatcher({
      registry: activation.actorHandlers,
      spot: activation.spot,
      providerResolver: this.options.providerResolver,
      serial: activation.serial,
      messageSerializers: this.options.messageSerializers
    });
  }
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted === true) {
    throw new Error('The operation was aborted.');
  }
}
