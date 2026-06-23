import type { Type, ZLinkEncodedPayload } from '../Common';

export interface ZLinkMessageSerializer {
  serialize<T>(value: T): ZLinkEncodedPayload;
  deserialize<T>(payload: ZLinkEncodedPayload, type: Type<T>): T;
}

export function parseMessage<T>(_payload: ZLinkEncodedPayload, _type: Type<T>): T {
  throw new Error('No ZLinkMessageSerializer is registered.');
}
