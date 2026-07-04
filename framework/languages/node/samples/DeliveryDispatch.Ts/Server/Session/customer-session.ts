import { Inject } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import {
  actorRefFromMessage,
  ensureCustomerActor,
  PacketNames,
  subscribeCustomerToDelivery
} from '../../Shared/Contracts/messages';
import { retry } from '../Configuration/request-retry';
import { CustomerSessionDirectory } from './customer-session-directory';
import type {
  ZLinkChannelClient,
  ZLinkMessage,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import type {
  EnsureCustomerActorRes,
  SubscribeCustomerToDeliveryRes,
  SubscribeDeliveryReq,
  SubscribeDeliveryRes
} from '../../Shared/Contracts/messages';

const CustomerId = 'customer-1';

class CustomerSession implements ZLinkSession {
  constructor(
    readonly context: ZLinkSessionContext,
    private readonly channels: ZLinkChannelClient,
    private readonly sessions: CustomerSessionDirectory
  ) {}

  async onConnected(context: ZLinkSessionContext): Promise<void> {
    this.sessions.add(context);
  }

  async onDisconnected(context: ZLinkSessionContext): Promise<void> {
    this.sessions.remove(context);
  }

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
    console.error(`deliverydispatch session: ensure customer delivery=${request.deliveryId}`);
    const ensured = await retry(() => this.channels
      .requestToChannel(SampleNames.trackingChannel, ensureCustomerActor(CustomerId))
      .timeout(500)
      .submit<EnsureCustomerActorRes>(), { delayMs: 100, maxAttempts: 40 });
    await this.context.actors.bindOrGet(actorRefFromMessage(ensured.actor));
    console.error(`deliverydispatch session: bound customer actor=${ensured.actor.actorId}`);

    console.error(`deliverydispatch session: subscribe delivery=${request.deliveryId}`);
    const subscribed = await retry(() => this.channels
      .requestToChannel(SampleNames.trackingChannel, subscribeCustomerToDelivery(CustomerId, request.deliveryId))
      .timeout(500)
      .submit<SubscribeCustomerToDeliveryRes>(), { delayMs: 100, maxAttempts: 40 });
    this.sessions.subscribe(this.context, subscribed.deliveryId);
    console.error(`deliverydispatch session: reply subscribed delivery=${subscribed.deliveryId}`);
    await this.context.client.reply({ deliveryId: subscribed.deliveryId } satisfies SubscribeDeliveryRes).submit();
  }
}

class CustomerSessionFactory implements ZLinkSessionFactory<CustomerSession> {
  constructor(
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient,
    private readonly sessions: CustomerSessionDirectory
  ) {}

  create(context: ZLinkSessionContext): CustomerSession {
    return new CustomerSession(context, this.channels, this.sessions);
  }
}

export {
  CustomerSession,
  CustomerSessionFactory
};
