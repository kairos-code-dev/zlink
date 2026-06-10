import type { RoutingId } from './CoreTypes';

export interface ZLinkMessageMetadata {
  readonly channelName?: string;
  readonly packetName?: string;
  readonly sourceNodeRid?: RoutingId;
  readonly sourceSessionRid?: RoutingId;
}

export interface ZLinkMessageMetadataPolicy {
  readonly forward: boolean;
}
