import type { ZLinkRequestCall, ZLinkSendCall } from './Calls';

export interface ZLinkChannelClient {
  send<TMessage>(message: TMessage): ZLinkSendCall;
  request<TRequest>(request: TRequest): ZLinkRequestCall;
  sendToChannel<TMessage>(channelName: string, message: TMessage): ZLinkSendCall;
  requestToChannel<TRequest>(channelName: string, request: TRequest): ZLinkRequestCall;
}
