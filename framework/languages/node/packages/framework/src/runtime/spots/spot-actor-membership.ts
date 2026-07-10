import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkMessageSerializer,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse
} from '../../contracts';
import { ZLinkEncodedPayload, ZLinkMessage } from '../../contracts';
import { throwIfAborted } from '../abort';
import { routingIdsEqual } from '../routing-id';
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
import type { ZLinkSpotActivation } from './spot-activation-state';
import type { ZLinkSpotActorTransferRuntime } from './spot-runtime-ports';

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
  readonly actorTransferRuntime?: ZLinkSpotActorTransferRuntime;
}

export type ZLinkActorJoinRollback = () => Promise<void> | void;

export class ZLinkSpotActorMembership {
  constructor(private readonly options: ZLinkSpotActorMembershipOptions) {}

  async admitActorJoin(
    spotRid: RoutingId,
    actor: ZLinkActor,
    request: Message,
    commit: (spot: ZLinkSpot) => Promise<ZLinkActorJoinRollback | void> | ZLinkActorJoinRollback | void,
    signal?: AbortSignal
  ): Promise<ZLinkSpotActorJoinResponse> {
    throwIfAborted(signal);
    const activation = this.requireActivation(spotRid);
    const dispatcher = this.createActorDispatcher(activation);
    const transaction: {
      rollbackExternal?: ZLinkActorJoinRollback;
      rollbackMembership?: () => void;
      committed: boolean;
    } = { committed: false };
    let response: ZLinkSpotActorJoinResponse;
    try {
      response = await dispatcher.admitActorJoin(actor, request, async () => {
        await this.options.entrySpotCallbacks?.onLeaveActor(actor, signal);
        const rollback = await commit(activation.spot);
        if (rollback !== undefined) transaction.rollbackExternal = rollback;
        transaction.rollbackMembership = activation.commitActorJoin(actor);
        transaction.committed = true;
      });
    } catch (error) {
      if (transaction.committed) {
        transaction.rollbackMembership?.();
        await transaction.rollbackExternal?.();
      }
      throw error;
    }
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
      activation.beginActorTransfer(actor.actorId);
      await activation.spot.onLeaveActor?.(actor, signal);
      activation.commitActorDeparture(actor.actorId);
      this.options.actorTransferRuntime?.clearRoutedActor(actor);
    });
    const localEntryNodeRid =
      this.options.entryNodeRidProvider?.() ??
      this.options.entryNodeRid ??
      this.options.nodeRid;
    const entryNodeRid = this.options.actorTransferRuntime?.actorEntryNodeRid(actor) ??
      localEntryNodeRid;
    if (entryNodeRid === undefined) {
      throw new ZLinkConfigurationException('Spot actor leave requires an Entry Spot node routing id.');
    }
    const actorRef = (actor.context as unknown as { actorRef?: ActorRef }).actorRef;
    if (
      localEntryNodeRid !== undefined &&
      actorRef?.nodeRid !== undefined &&
      !routingIdsEqual(actorRef.nodeRid, localEntryNodeRid) &&
      !routingIdsEqual(entryNodeRid, localEntryNodeRid)
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

  async notifyActorLeftAfterTransfer(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const activation = this.requireActivation(spotRid);
    await activation.serial.execute(async () => {
      activation.beginActorTransfer(actor.actorId);
      await activation.spot.onLeaveActor?.(actor, signal);
      activation.commitActorDeparture(actor.actorId);
    });
  }

  async beginActorTransfer(spotRid: RoutingId, actorId: string): Promise<void> {
    const activation = this.requireActivation(spotRid);
    await activation.serial.execute(() => {
      activation.beginActorTransfer(actorId);
    });
  }

  async cancelActorTransfer(spotRid: RoutingId, actorId: string): Promise<void> {
    const activation = this.requireActivation(spotRid);
    await activation.serial.execute(() => {
      activation.cancelActorTransfer(actorId);
    });
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
    const joinedActor = activation.resolveJoinedActor(actor.actorId);
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
