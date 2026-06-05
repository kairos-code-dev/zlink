// SPDX-License-Identifier: MPL-2.0

import { Message } from './message';
import { RoutingId } from '../core/routing_id';
import { RecvError, RecvResult } from '../errors/errors';

function freezeMessageParts(parts: readonly Message[]): Message[] {
  return Object.freeze(parts.slice()) as Message[];
}

function invalidMultipartError(partsLength: number): RecvError {
  const error = new RecvError(RecvResult.NotSupported, 0);
  error.message = `expected exactly 1 part but received ${partsLength}`;
  return error;
}

function missingPartError(): RecvError {
  const error = new RecvError(RecvResult.NotSupported, 0);
  error.message = 'message has no parts';
  return error;
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
  constructor() {
    this.parts = freezeMessageParts([]);
    this.routingId = null;
    this.topic = '';
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

}
