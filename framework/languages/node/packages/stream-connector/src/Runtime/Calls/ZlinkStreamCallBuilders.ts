import {
  RequiredZlinkStreamConnectorOptions,
  ZlinkStreamEncodedPayload,
  ZlinkStreamErrorCode,
  ZlinkStreamMessage,
  ZlinkStreamMessageKind,
  ZlinkStreamMetadata,
  ZlinkStreamMetadataMap,
  type ZlinkStreamRequestCall,
  type ZlinkStreamResultOf,
  type ZlinkStreamSendCall,
  type ZlinkStreamWaitCall
} from '../../Contracts';
import { connectorError, unwrapStreamError } from '../ZlinkStreamSupport';
import { validateName } from '../Protocol/ZlinkStreamPacketNameValidator';

interface ZlinkStreamConnectorSubmitter {
  readonly options: RequiredZlinkStreamConnectorOptions;
  sendEncoded(
    kind: ZlinkStreamMessageKind,
    name: string,
    payload: ZlinkStreamEncodedPayload,
    metadata: ZlinkStreamMetadata,
    compress: boolean,
    requestSeq: bigint | undefined,
    signal?: AbortSignal
  ): Promise<void>;
  requestEncoded(
    name: string,
    payload: ZlinkStreamEncodedPayload,
    metadata: ZlinkStreamMetadata,
    compress: boolean,
    timeoutMs: number,
    signal?: AbortSignal
  ): Promise<ZlinkStreamEncodedPayload>;
  waitForMessage<TPayload>(
    name: string,
    timeoutMs: number,
    predicate: (message: ZlinkStreamMessage<TPayload>) => boolean,
    signal?: AbortSignal
  ): Promise<ZlinkStreamMessage<TPayload>>;
}

class ZlinkStreamCallBuilderState {
  private executed = false;
  name: string | undefined;
  metadata: ZlinkStreamMetadata = ZlinkStreamMetadataMap.empty;
  timeoutMs: number | undefined;
  compress = false;

  constructor(name: string | undefined) {
    this.name = name;
  }

  ensureNotExecuted(): void {
    if (this.executed) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Builder instances can be executed only once.');
    }
    this.executed = true;
  }

  resolveMessageName(): string {
    if (this.name === undefined) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Message name is required when the encoded stream payload has no message type.');
    }
    return this.name;
  }
}

export class ZlinkStreamSendBuilder implements ZlinkStreamSendCall {
  private readonly state: ZlinkStreamCallBuilderState;

  constructor(
    private readonly connector: ZlinkStreamConnectorSubmitter,
    name: string | undefined,
    private readonly payload: ZlinkStreamEncodedPayload
  ) {
    this.state = new ZlinkStreamCallBuilderState(name);
  }

  packetName(name: string): this {
    validateName(name);
    this.state.name = name;
    return this;
  }

  metadata(key: string, value: string): this;
  metadata(metadata: ZlinkStreamMetadata): this;
  metadata(keyOrMetadata: string | ZlinkStreamMetadata, value?: string): this {
    this.state.metadata = typeof keyOrMetadata === 'string'
      ? this.state.metadata.with(keyOrMetadata, value ?? '')
      : keyOrMetadata;
    return this;
  }

  compress(): this {
    this.state.compress = true;
    return this;
  }

  submit(signal?: AbortSignal): Promise<void> {
    this.state.ensureNotExecuted();
    return this.connector.sendEncoded(
      ZlinkStreamMessageKind.Send,
      this.state.resolveMessageName(),
      this.payload,
      this.state.metadata,
      this.state.compress,
      undefined,
      signal
    );
  }
}

export class ZlinkStreamRequestBuilder implements ZlinkStreamRequestCall {
  private readonly state: ZlinkStreamCallBuilderState;

  constructor(
    private readonly connector: ZlinkStreamConnectorSubmitter,
    name: string | undefined,
    private readonly payload: ZlinkStreamEncodedPayload
  ) {
    this.state = new ZlinkStreamCallBuilderState(name);
  }

  packetName(name: string): this {
    validateName(name);
    this.state.name = name;
    return this;
  }

  metadata(key: string, value: string): this;
  metadata(metadata: ZlinkStreamMetadata): this;
  metadata(keyOrMetadata: string | ZlinkStreamMetadata, value?: string): this {
    this.state.metadata = typeof keyOrMetadata === 'string'
      ? this.state.metadata.with(keyOrMetadata, value ?? '')
      : keyOrMetadata;
    return this;
  }

  timeout(timeoutMs: number): this {
    this.state.timeoutMs = timeoutMs;
    return this;
  }

  compress(): this {
    this.state.compress = true;
    return this;
  }

  submit<TReply = ZlinkStreamEncodedPayload>(signal?: AbortSignal): Promise<TReply>;
  submit(callback: (result: ZlinkStreamResultOf<ZlinkStreamEncodedPayload>) => void): void;
  submit<TReply = ZlinkStreamEncodedPayload>(
    signalOrCallback?: AbortSignal | ((result: ZlinkStreamResultOf<ZlinkStreamEncodedPayload>) => void)
  ): Promise<TReply> | void {
    this.state.ensureNotExecuted();
    const operation = this.connector.requestEncoded(
      this.state.resolveMessageName(),
      this.payload,
      this.state.metadata,
      this.state.compress,
      this.state.timeoutMs ?? this.connector.options.requestTimeoutMs,
      typeof signalOrCallback === 'function' ? undefined : signalOrCallback
    );
    if (typeof signalOrCallback === 'function') {
      operation.then(
        (value) => signalOrCallback({ isSuccess: true, value }),
        (error) => signalOrCallback({ isSuccess: false, error: unwrapStreamError(error) })
      );
      return;
    }
    return operation.then((value) => this.connector.options.codec?.decode<TReply>(value) ?? (value as TReply));
  }
}

export class ZlinkStreamWaitBuilder<TPayload = ZlinkStreamEncodedPayload> implements ZlinkStreamWaitCall<TPayload> {
  private executed = false;
  private timeoutMs: number | undefined;
  private predicate: (message: ZlinkStreamMessage<TPayload>) => boolean = () => true;

  constructor(
    private readonly connector: ZlinkStreamConnectorSubmitter,
    private readonly name: string
  ) {}

  where(predicate: (message: ZlinkStreamMessage<TPayload>) => boolean): this {
    this.ensureConfigurable();
    this.predicate = predicate;
    return this;
  }

  timeout(timeoutMs: number): this {
    this.ensureConfigurable();
    this.timeoutMs = timeoutMs;
    return this;
  }

  submit(signal?: AbortSignal): Promise<ZlinkStreamMessage<TPayload>> {
    this.markExecuted();
    return this.connector.waitForMessage(
      this.name,
      this.timeoutMs ?? this.connector.options.requestTimeoutMs,
      this.predicate,
      signal
    );
  }

  private ensureConfigurable(): void {
    if (this.executed) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Builder instances can be executed only once.');
    }
  }

  private markExecuted(): void {
    this.ensureConfigurable();
    this.executed = true;
  }
}
