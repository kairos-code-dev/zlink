import type { Message, Type } from '../Common';

export interface ZLinkMessageSerializer {
  serialize<T>(value: T): Message;
  deserialize<T>(message: Message, type: Type<T>): T;
}

export function parseMessage<T>(_message: Message, _type: Type<T>): T {
  throw new Error('No ZLinkMessageSerializer is registered.');
}
