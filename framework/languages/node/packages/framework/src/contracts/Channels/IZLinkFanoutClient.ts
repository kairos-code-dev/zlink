import type { ZLinkPublishCall } from './Calls';

export interface ZLinkFanoutClient {
  publish(topic: string, event: unknown): ZLinkPublishCall;
  publishToChannel(channelName: string, topic: string, event: unknown): ZLinkPublishCall;
}
