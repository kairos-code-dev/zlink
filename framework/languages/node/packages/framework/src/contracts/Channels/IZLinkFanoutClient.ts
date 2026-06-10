import type { ZLinkPublishCall } from './Calls';

export interface ZLinkFanoutClient {
  publish<TEvent>(topic: string, event: TEvent): ZLinkPublishCall;
  publishToChannel<TEvent>(channelName: string, topic: string, event: TEvent): ZLinkPublishCall;
}
