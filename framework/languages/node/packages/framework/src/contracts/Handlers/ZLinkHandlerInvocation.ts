import type { ZLinkMessageContext } from './Contexts';

export interface ZLinkHandlerInvocation {
  readonly ownerKind: string;
  readonly messageContext: ZLinkMessageContext;
}
