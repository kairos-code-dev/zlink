import type {
  RoutingId,
  ZLinkActor,
  ZLinkMessageSerializer,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse
} from '../../contracts';
import { ZLinkEncodedPayload, ZLinkMessage } from '../../contracts';
import { throwIfAborted } from '../abort';
import type { Message } from '../../contracts/Common/Message';
import {
  Message as BindingMessage
} from '@zlink-systems/zlink';
import {
  ZLinkConfigurationException
} from '../configuration';
import { ZLinkDispatchErrorReporter } from '../channels';
import { createActorMembership, ZLinkSpotActorDispatcher } from '../actors';
import {
  encodeFrameworkPayloadMessage
} from '../messaging/payload-codec';
import { routingIdsEqual } from '../routing-id';
import type { ZLinkSpotActivation } from './spot-activation-state';
import type { ZLinkSpotActorTransferRuntime } from './spot-runtime-ports';

export interface ZLinkSpotActorMembershipOptions {
  readonly resolveActivation: (
    spotRid: RoutingId,
    meshName?: string
  ) => ZLinkSpotActivation | undefined;
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
    signal?: AbortSignal,
    leaveSource?: () => Promise<void>
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
      response = await dispatcher.evaluateActorJoin(actor, request);
      if (response.accepted) {
        if (leaveSource === undefined) {
          await this.options.entrySpotCallbacks?.onLeaveActor(actor, signal);
        } else {
          await leaveSource();
        }
        await dispatcher.commitActorJoin(actor, async () => {
          const rollback = await commit(activation.spot);
          if (rollback !== undefined) transaction.rollbackExternal = rollback;
          transaction.rollbackMembership = activation.commitActorJoin(actor);
          transaction.committed = true;
        });
      }
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
    signal?: AbortSignal,
    meshName?: string
  ): Promise<void> {
    throwIfAborted(signal);
    const activation = this.requireActivation(spotRid, meshName);
    const localEntryNodeRid =
      this.options.entryNodeRidProvider?.() ??
      this.options.entryNodeRid ??
      this.options.nodeRid;
    const entryNodeRid = this.options.actorTransferRuntime?.actorEntryNodeRid(actor) ??
      localEntryNodeRid;
    if (entryNodeRid === undefined) {
      throw new ZLinkConfigurationException('Spot actor leave requires an Entry Spot node routing id.');
    }
    const remoteEntry = localEntryNodeRid !== undefined && !routingIdsEqual(entryNodeRid, localEntryNodeRid);
    if (!remoteEntry) {
      await activation.serial.execute(async () => {
        activation.beginActorTransfer(actor.actorId);
        await activation.spot.onLeaveActor(createActorMembership(actor));
        activation.commitActorDeparture(actor.actorId);
        this.options.actorTransferRuntime?.clearRoutedActor(actor);
      });
    }
    const request = BindingMessage.from(Buffer.alloc(0));
    try {
      await actor.context.joinEntrySpot(
        entryNodeRid,
        ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(request.data()))
      ).submit(signal);
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
      await activation.spot.onLeaveActor(createActorMembership(actor));
      activation.commitActorDeparture(actor.actorId);
    });
  }


  async prepareActorLeaveForTransfer(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const activation = this.requireActivation(spotRid);
    await activation.serial.execute(() => activation.spot.onLeaveActor(createActorMembership(actor)));
  }

  async commitActorLeaveAfterTransfer(spotRid: RoutingId, actorId: string): Promise<void> {
    const activation = this.requireActivation(spotRid);
    await activation.serial.execute(() => activation.commitActorDeparture(actorId));
  }

  async restoreActorAfterFailedTransfer(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const activation = this.requireActivation(spotRid);
    await activation.serial.execute(async () => {
      activation.cancelActorTransfer(actor.actorId);
      await activation.spot.onJoinedActor(createActorMembership(actor));
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
    await activation.serial.execute(() =>
      activation.spot.onDisconnectActor(createActorMembership(joinedActor)));
    return true;
  }

  private requireActivation(spotRid: RoutingId, meshName?: string): ZLinkSpotActivation {
    const activation = this.options.resolveActivation(spotRid, meshName);
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
