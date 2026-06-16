import type { ZLinkRequestCall, ZLinkSendCall } from './Calls';

export interface ZLinkChannelClient {
  send(message: unknown): ZLinkSendCall;
  request(request: unknown): ZLinkRequestCall;
  sendToChannel(channelName: string, message: unknown): ZLinkSendCall;
  requestToChannel(channelName: string, request: unknown): ZLinkRequestCall;
}
