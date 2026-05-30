// SPDX-License-Identifier: MPL-2.0

import { RecvError, RecvResult } from '../errors/errors';
import { Message } from './message';

export function freezeMessageParts(parts: readonly Message[]): Message[] {
  return Object.freeze(parts.slice()) as Message[];
}

export function freezeOwnedMessageParts(parts: Message[]): Message[] {
  return Object.freeze(parts) as Message[];
}

export function invalidMultipartError(partsLength: number): RecvError {
  return new RecvError(
    RecvResult.NotSupported,
    0,
    `expected exactly 1 part but received ${partsLength}`
  );
}

export function missingPartError(): RecvError {
  return new RecvError(RecvResult.NotSupported, 0, 'message has no parts');
}

/** Base for received multipart envelopes; owns its message parts until closed. */
export class MultipartEnvelope {
  /** The message parts, owned by this envelope. */
  parts: Message[];

  constructor(parts: readonly Message[]) {
    this.parts = freezeMessageParts(parts);
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

  protected replaceParts(parts: readonly Message[]): void {
    this.close();
    this.parts = freezeMessageParts(parts);
  }
}
