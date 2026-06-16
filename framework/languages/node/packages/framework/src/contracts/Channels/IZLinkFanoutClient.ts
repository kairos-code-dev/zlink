import type { ZLinkPublishCall } from './Calls';
import type { Message } from '../Common';

export interface ZLinkFanoutClient {
  publish(topic: string, event: Message): ZLinkPublishCall;
  publishToChannel(channelName: string, topic: string, event: Message): ZLinkPublishCall;
}
