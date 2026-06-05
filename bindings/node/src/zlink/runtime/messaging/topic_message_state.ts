// SPDX-License-Identifier: MPL-2.0

import { TopicMessage } from '../../contracts/messaging/topic_message';
import { Message } from '../../contracts/messaging/message';
import { RoutingId } from '../../contracts/core/routing_id';

interface TopicMessageState {
  parts: Message[];
  routingId: RoutingId | null;
  topic: string;
}

function freezeMessageParts(parts: readonly Message[]): Message[] {
  return Object.freeze(parts.slice()) as Message[];
}

function freezeOwnedMessageParts(parts: Message[]): Message[] {
  return Object.freeze(parts) as Message[];
}

export function createTopicMessage(
  topic: string,
  parts: readonly Message[],
  routingId: RoutingId | null = null
): TopicMessage {
  const message = new TopicMessage();
  replaceTopicMessage(message, topic, freezeMessageParts(parts), routingId);
  return message;
}

export function adoptTopicMessageFrom(target: TopicMessage, source: TopicMessage): void {
  target.close();
  const targetState = target as unknown as TopicMessageState;
  const sourceState = source as unknown as TopicMessageState;
  targetState.parts = freezeMessageParts(source.parts);
  sourceState.parts = [];
  targetState.routingId = source.routingId;
  targetState.topic = source.topic;
  sourceState.routingId = null;
  sourceState.topic = '';
}

export function replaceTopicMessage(
  target: TopicMessage,
  topic: string,
  parts: Message[],
  routingId: RoutingId | null = null
): void {
  const state = target as unknown as TopicMessageState;
  state.parts = Object.isFrozen(parts) ? parts : freezeOwnedMessageParts(parts);
  state.routingId = routingId;
  state.topic = topic;
}
