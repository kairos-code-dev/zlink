import type { ZLinkPublishCall } from './Calls';

export interface ZLinkFanoutClient {
  publish(channelName: string, topic: string, event: unknown): ZLinkPublishCall;
}
