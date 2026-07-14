export interface ZLinkSendCall {
  submit(): void;
}

export interface ZLinkRequestCall {
  timeout(timeoutMs: number): this;
  submit<TReply>(signal?: AbortSignal): Promise<TReply>;
  yield<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkPublishCall {
  submit(): void;
}
