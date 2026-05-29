// SPDX-License-Identifier: MPL-2.0

import { Message } from './message';
import { RoutingId } from '../core/routing_id';
import { freezeOwnedMessageParts, MultipartEnvelope } from './envelope';

const TOPIC_MESSAGE_CREATE_TOKEN = Symbol('topic-message.create');

export class TopicMessage extends MultipartEnvelope {
  routingId: RoutingId | null;
  topic: string;

  constructor();
  constructor(
    token: symbol,
    topic: string,
    parts: readonly Message[],
    routingId?: RoutingId | null
  );
  constructor(
    token?: symbol,
    topic: string = '',
    parts: readonly Message[] = [],
    routingId: RoutingId | null = null
  ) {
    if (token === undefined) {
      super([]);
      this.routingId = null;
      this.topic = '';
      return;
    }
    if (token !== TOPIC_MESSAGE_CREATE_TOKEN) {
      throw new TypeError('TopicMessage values are created by subscribe operations');
    }
    super(parts);
    this.routingId = routingId;
    this.topic = topic;
  }

  /** @internal */
  static create(
    topic: string,
    parts: readonly Message[],
    routingId: RoutingId | null = null
  ): TopicMessage {
    return new TopicMessage(TOPIC_MESSAGE_CREATE_TOKEN, topic, parts, routingId);
  }

  /** @internal */
  adoptFrom(source: TopicMessage): void {
    this.replaceParts(source.parts);
    source.parts = [];
    this.routingId = source.routingId;
    this.topic = source.topic;
    source.routingId = null;
    source.topic = '';
  }

  /** @internal */
  _replace(
    topic: string,
    parts: Message[],
    routingId: RoutingId | null = null
  ): void {
    for (const part of this.parts) {
      try { part.close(); } catch { /* swallow */ }
    }
    this.parts = freezeOwnedMessageParts(parts);
    this.routingId = routingId;
    this.topic = topic;
  }
}
