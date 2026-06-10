import type { ZLinkHandlerDelegate } from './ZLinkHandlerDelegate';
import type { ZLinkHandlerInvocation } from './ZLinkHandlerInvocation';

export interface ZLinkHandlerFilter {
  invoke(invocation: ZLinkHandlerInvocation, next: ZLinkHandlerDelegate): Promise<unknown>;
}
