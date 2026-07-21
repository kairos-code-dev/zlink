import type { ZLinkRequestCall, ZLinkSendCall } from './Calls';

export interface ZLinkChannelClient {
  sendToChannel(meshName: string, channelName: string, message: unknown): ZLinkSendCall;
  requestToChannel(meshName: string, channelName: string, request: unknown): ZLinkRequestCall;
}
