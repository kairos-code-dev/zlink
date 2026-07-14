import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import {
  PacketNames,
  SubscribeDeliveryRes
} from '../../Shared/Contracts/messages';
import { CustomerActorDirectory } from './customer-actor';
import type {
  ZLinkActorManager,
  ZLinkMessage,
  ZLinkLocationStore,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import type {
  SubscribeDeliveryReq
} from '../../Shared/Contracts/messages';

const CustomerId = 'customer-1';

class CustomerSession implements ZLinkSession {
  constructor(
    readonly context: ZLinkSessionContext,
    private readonly actors: ZLinkActorManager,
    private readonly directory: CustomerActorDirectory,
    private readonly spotRid: string,
    private readonly locations: ZLinkLocationStore
  ) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    console.error(`deliverydispatch session: dispatch packet=${dispatch.packetName}`);
    if (dispatch.packetName !== PacketNames.subscribeDelivery) {
      const actor = this.context.actors.find(CustomerId);
      if (actor === undefined) {
        throw new Error(`No customer actor is bound for packet '${dispatch.packetName}'.`);
      }
      await actor.relay(payload);
      return;
    }

    const request = payload.decode<SubscribeDeliveryReq>(Object as never);
    console.error(`deliverydispatch session: find customer delivery=${request.deliveryId}`);
    let resolved = await this.locations.resolveActor({ actorId: CustomerId });
    if (resolved === undefined) {
      await this.actors.getOrCreate(CustomerId, SampleNames.customerActorType, { customerId: CustomerId });
      const created = this.directory.require(CustomerId);
      await created.ensureJoined(this.spotRid, payload);
      resolved = await this.locations.resolveActor({ actorId: CustomerId });
    } else {
      console.error(`deliverydispatch session: found existing customer=${CustomerId}`);
    }
    if (resolved?.actorRef === undefined) {
      throw new Error(`Customer actor '${CustomerId}' was not registered in the location store.`);
    }
    const active = this.directory.require(CustomerId);
    active.subscribe(request.deliveryId);
    await this.context.actors.bindOrGet(resolved.actorRef);
    console.error(`deliverydispatch session: bound customer actor=${resolved.actorRef.actorId}`);
    await this.context.client.reply(new SubscribeDeliveryRes(request.deliveryId)).submit();
  }
}

class CustomerSessionFactory implements ZLinkSessionFactory<CustomerSession> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
    private readonly directory: CustomerActorDirectory,
    @Inject('DELIVERYDISPATCH_CUSTOMER_SPOT_RID') private readonly spotRid: string,
    @Inject('DELIVERYDISPATCH_LOCATION_STORE') private readonly locations: ZLinkLocationStore
  ) {}

  async create(context: ZLinkSessionContext): Promise<CustomerSession> {
    return new CustomerSession(context, this.actors, this.directory, this.spotRid, this.locations);
  }
}

export {
  CustomerSession,
  CustomerSessionFactory
};
