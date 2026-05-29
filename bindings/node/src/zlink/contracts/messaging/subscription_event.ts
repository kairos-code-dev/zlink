// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../core/routing_id';

const SUBSCRIPTION_EVENT_CREATE_TOKEN = Symbol('subscription-event.create');

export class SubscriptionEvent {
  routingId: RoutingId | null;
  topic: string;
  subscribed: boolean;

  constructor();
  constructor(
    token: symbol,
    topic: string,
    subscribed: boolean,
    routingId?: RoutingId | null
  );
  constructor(
    token?: symbol,
    topic: string = '',
    subscribed: boolean = false,
    routingId: RoutingId | null = null
  ) {
    if (token === undefined) {
      this.routingId = null;
      this.topic = '';
      this.subscribed = false;
      return;
    }
    if (token !== SUBSCRIPTION_EVENT_CREATE_TOKEN) {
      throw new TypeError('SubscriptionEvent values are created by subscription event operations');
    }
    this.routingId = routingId;
    this.topic = topic;
    this.subscribed = subscribed === true;
  }

  /** @internal */
  static create(
    topic: string,
    subscribed: boolean,
    routingId: RoutingId | null = null
  ): SubscriptionEvent {
    return new SubscriptionEvent(SUBSCRIPTION_EVENT_CREATE_TOKEN, topic, subscribed, routingId);
  }

  /** @internal */
  adoptFrom(source: SubscriptionEvent): void {
    this.routingId = source.routingId;
    this.topic = source.topic;
    this.subscribed = source.subscribed;
  }
}
