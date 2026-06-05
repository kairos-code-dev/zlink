// SPDX-License-Identifier: MPL-2.0

import { Message } from '../../contracts';

export interface MessageSnapshot {
  data: Buffer;
  refCount?: number;
  properties?: Readonly<Record<string, string>>;
  metadata?: Readonly<Map<number, Buffer>>;
}

export function messageFromSnapshot(snapshot: MessageSnapshot): Message {
  return (Message as unknown as {
    fromSnapshot(snapshot: MessageSnapshot): Message;
  }).fromSnapshot(snapshot);
}

export function messageToSnapshot(message: Message): MessageSnapshot {
  return (message as unknown as {
    toSnapshot(): MessageSnapshot;
  }).toSnapshot();
}
