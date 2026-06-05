// SPDX-License-Identifier: MPL-2.0

import { Message } from './message';
import { RoutingId } from '../core/routing_id';
import { RecvError, RecvResult } from '../errors/errors';

const TOPIC_MESSAGE_CREATE_TOKEN = Symbol('topic-message.create');

function freezeMessageParts(parts: readonly Message[]): Message[] {
  return Object.freeze(parts.slice()) as Message[];
}

function freezeOwnedMessageParts(parts: Message[]): Message[] {
  return Object.freeze(parts) as Message[];
}

function invalidMultipartError(partsLength: number): RecvError {
  return new RecvError(
    RecvResult.NotSupported,
    0,
    `expected exactly 1 part but received ${partsLength}`
  );
}

function missingPartError(): RecvError {
  return new RecvError(RecvResult.NotSupported, 0, 'message has no parts');
}

/**
 * A received publish: its topic and message parts. Owns its parts until closed.
 */
export class TopicMessage {
  /** The message parts, owned by this envelope. */
  parts: Message[];
  /** The source routing id, or null when the receive path provides none. */
  routingId: RoutingId | null;
  /** The topic the message was published under. */
  topic: string;

  /** Create an empty reusable envelope for use with `subscribe`. */
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
      this.parts = freezeMessageParts([]);
      this.routingId = null;
      this.topic = '';
      return;
    }
    if (token !== TOPIC_MESSAGE_CREATE_TOKEN) {
      throw new TypeError('TopicMessage values are created by subscribe operations');
    }
    this.parts = freezeMessageParts(parts);
    this.routingId = routingId;
    this.topic = topic;
  }

  /** Return true when the envelope holds exactly one part. */
  isSinglePart(): boolean {
    return this.parts.length === 1;
  }

  /** Return the first part without transferring ownership; throws when the envelope has no parts. */
  firstPart(): Message {
    if (this.parts.length === 0) {
      throw missingPartError();
    }
    return this.parts[0];
  }

  /** Return the only part; throws unless the envelope holds exactly one part. */
  singlePartOrThrow(): Message {
    if (!this.isSinglePart()) {
      throw invalidMultipartError(this.parts.length);
    }
    return this.parts[0];
  }

  /** Close every part, releasing their storage. */
  close(): void {
    for (const part of this.parts) {
      part.close();
    }
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
    this.close();
    this.parts = freezeMessageParts(source.parts);
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
    this.parts = freezeOwnedMessageParts(parts);
    this.routingId = routingId;
    this.topic = topic;
  }
}
