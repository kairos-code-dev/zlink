import type { ZLinkHandlerContext } from './Contexts';

export interface ZLinkHandlerInvocation {
  readonly message: unknown;
  readonly context: ZLinkHandlerContext;
  readonly channelName?: string;
  readonly packetName?: string;
}
