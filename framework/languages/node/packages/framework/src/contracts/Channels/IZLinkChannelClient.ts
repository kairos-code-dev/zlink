import type { ZLinkRequestCall, ZLinkSendCall } from './Calls';
import type { Message } from '../Common';

export interface ZLinkChannelClient {
  send(message: Message): ZLinkSendCall;
  request(request: Message): ZLinkRequestCall;
  sendToChannel(channelName: string, message: Message): ZLinkSendCall;
  requestToChannel(channelName: string, request: Message): ZLinkRequestCall;
}
