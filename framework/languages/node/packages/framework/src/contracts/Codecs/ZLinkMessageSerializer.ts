import type { Type, ZLinkEncodedPayload } from '../Common';

export interface ZLinkSerializerSelectionContext {
  readonly messageType?: Type<unknown>;
  readonly packetName?: string;
}

export interface ZLinkMessageSerializer {
  canSerialize?(value: unknown, context: ZLinkSerializerSelectionContext): boolean;
  serialize<T>(value: T): ZLinkEncodedPayload;
  deserialize<T>(payload: ZLinkEncodedPayload, type: Type<T>): T;
}

export function parseMessage<T>(_payload: ZLinkEncodedPayload, _type: Type<T>): T {
  throw new Error('No ZLinkMessageSerializer is registered.');
}
