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

export class MultipartEnvelope {
  parts: Message[];

  constructor(parts: readonly Message[]) {
    this.parts = freezeMessageParts(parts);
  }

  isSinglePart(): boolean {
    return this.parts.length === 1;
  }

  firstPart(): Message {
    if (this.parts.length === 0) {
      throw missingPartError();
    }
    return this.parts[0];
  }

  singlePartOrThrow(): Message {
    if (!this.isSinglePart()) {
      throw invalidMultipartError(this.parts.length);
    }
    return this.parts[0];
  }

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
