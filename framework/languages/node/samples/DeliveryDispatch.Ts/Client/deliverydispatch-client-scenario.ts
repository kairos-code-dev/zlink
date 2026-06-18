import { createDeliveryReq, serverAssertionReq, subscribeDeliveryReq } from '../Shared/Contracts/messages';
import { SampleNames } from '../Shared/Configuration/sample-names';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type { DeliveryCreated, ServerAssertionRes, SubscribeDeliveryAccepted } from '../Shared/Contracts/messages';

class DeliveryDispatchClientScenario {
  async run(channels: ZLinkChannelClient): Promise<void> {
    await this.runSuccessfulDelivery(channels);
    await this.runReassignedDelivery(channels);
    await this.assertServerEvidence(channels);
  }

  private async runSuccessfulDelivery(channels: ZLinkChannelClient): Promise<void> {
    const deliveryId = 'delivery-success';
    const subscribed = await channels
      .requestToChannel(SampleNames.dispatchChannel, subscribeDeliveryReq(deliveryId))
      .submit<SubscribeDeliveryAccepted>();
    ensure(subscribed.deliveryId === deliveryId);

    const created = await channels
      .requestToChannel(SampleNames.dispatchChannel, createDeliveryReq(
        deliveryId,
        'customer-1',
        'Kitchen 12',
        'Customer Lobby'
      ))
      .submit<DeliveryCreated>();
    ensure(created.deliveryId === deliveryId);
  }

  private async runReassignedDelivery(channels: ZLinkChannelClient): Promise<void> {
    const deliveryId = 'delivery-reassign';
    const subscribed = await channels
      .requestToChannel(SampleNames.dispatchChannel, subscribeDeliveryReq(deliveryId))
      .submit<SubscribeDeliveryAccepted>();
    ensure(subscribed.deliveryId === deliveryId);

    const created = await channels
      .requestToChannel(SampleNames.dispatchChannel, createDeliveryReq(
        deliveryId,
        'customer-1',
        'Kitchen 12',
        'Customer Lobby'
      ))
      .submit<DeliveryCreated>();
    ensure(created.deliveryId === deliveryId);
    console.log('deliverydispatch-reassignment=completed');
  }

  private async assertServerEvidence(channels: ZLinkChannelClient): Promise<void> {
    const assertion = await channels
      .requestToChannel(SampleNames.dispatchChannel, serverAssertionReq('delivery-success', 'delivery-reassign'))
      .submit<ServerAssertionRes>();
    ensure(assertion.passed);
    console.log('deliverydispatch-server-evidence=completed');
  }
}

function ensure(condition: boolean): void {
  if (!condition) {
    throw new Error('DeliveryDispatch scenario assertion failed.');
  }
}

export {
  DeliveryDispatchClientScenario
};
