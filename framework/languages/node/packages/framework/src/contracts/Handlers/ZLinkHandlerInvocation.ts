import type { ZLinkHandlerContext } from './Contexts';

export interface ZLinkHandlerInvocation {
  readonly context: ZLinkHandlerContext;
  readonly handler: unknown;
}
