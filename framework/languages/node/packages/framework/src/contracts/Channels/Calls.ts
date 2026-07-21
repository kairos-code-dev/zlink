import type { ZLinkMessageMetadata } from '../Common';
import type { ZLinkPublishResult, ZLinkSubmitResult } from '../RouteMesh';

export interface ZLinkSendCall {
  metadata(key: string, value: string): this;
  metadata(metadata: ZLinkMessageMetadata): this;
  submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}

export interface ZLinkRequestCall {
  metadata(key: string, value: string): this;
  metadata(metadata: ZLinkMessageMetadata): this;
  timeout(timeoutMs: number): this;
  submit<TReply>(signal?: AbortSignal): Promise<TReply>;
  yield<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkPublishCall {
  metadata(key: string, value: string): this;
  metadata(metadata: ZLinkMessageMetadata): this;
  submit(signal?: AbortSignal): Promise<ZLinkPublishResult>;
}

export interface ZLinkFanoutPublishCall {
  submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}
