import type {
  ZLinkBoundSessionSendCall,
  ZLinkSessionReplyCall,
  ZLinkSessionSendCall
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import {
  ensureSingleSubmit,
  resolvePacketName,
  type ZLinkStreamFrameHeader,
  ZLinkStreamMessageKind,
  type ZLinkStreamReplyMessageKind
} from './protocol';

export interface ZLinkBoundSessionSendRuntime {
  sendBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void>;
}

export interface ZLinkSessionCallContext {
  readonly stream: {
    writeRaw(payload: Message): boolean;
  };
  readonly dispatchHeader: ZLinkStreamFrameHeader | undefined;
  createJsonFrameMessage(
    kind: ZLinkStreamMessageKind,
    packetName: string,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    requestSeq: bigint | undefined,
    payload: unknown,
    correlationId?: string
  ): Message;
  createJsonReplyFrameMessage(
    requestHeader: ZLinkStreamFrameHeader,
    kind: ZLinkStreamReplyMessageKind,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    payload: unknown
  ): Message;
}

export class DefaultZLinkBoundSessionSendCall implements ZLinkBoundSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();
  private executed = false;

  constructor(
    private readonly runtime: ZLinkBoundSessionSendRuntime,
    private readonly actorId: string,
    private readonly message: unknown
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  packetName(packetName: string): this {
    this.selectedPacketName = packetName;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<void> {
    ensureSingleSubmit(this.executed);
    this.executed = true;
    await this.runtime.sendBoundSession(
      this.actorId,
      this.message,
      this.selectedPacketName,
      this.selectedMetadata,
      signal
    );
  }
}

export class DefaultZLinkSessionSendCall implements ZLinkSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();
  private compressionEnabled = false;
  private executed = false;

  constructor(
    private readonly context: ZLinkSessionCallContext,
    private readonly message: unknown
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  packetName(packetName: string): this {
    this.selectedPacketName = packetName;
    return this;
  }

  compress(enabled = true): this {
    this.compressionEnabled = enabled;
    return this;
  }

  submit(signal?: AbortSignal): void {
    throwIfAborted(signal);
    ensureSingleSubmit(this.executed);
    this.executed = true;
    const message = this.context.createJsonFrameMessage(
      ZLinkStreamMessageKind.Send,
      resolvePacketName(this.message, this.selectedPacketName),
      this.selectedMetadata,
      this.compressionEnabled,
      undefined,
      this.message
    );
    try {
      if (!this.context.stream.writeRaw(message)) {
        throw new Error('Client stream send failed.');
      }
    } finally {
      message.close();
    }
  }
}

export class DefaultZLinkSessionReplyCall implements ZLinkSessionReplyCall {
  private readonly selectedMetadata = new Map<string, string>();
  private compressionEnabled = false;
  private executed = false;

  constructor(
    private readonly context: ZLinkSessionCallContext,
    private readonly message: unknown
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  compress(enabled = true): this {
    this.compressionEnabled = enabled;
    return this;
  }

  submit(signal?: AbortSignal): void {
    throwIfAborted(signal);
    ensureSingleSubmit(this.executed);
    this.executed = true;
    const requestHeader = this.context.dispatchHeader;
    if (requestHeader?.requestSeq === undefined) {
      throw new Error('Reply is only available while handling a request packet.');
    }
    const message = this.context.createJsonReplyFrameMessage(
      requestHeader,
      ZLinkStreamMessageKind.Response,
      this.selectedMetadata,
      this.compressionEnabled,
      this.message
    );
    try {
      if (!this.context.stream.writeRaw(message)) {
        throw new Error('Client stream reply send failed.');
      }
    } finally {
      message.close();
    }
  }
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted === true) {
    throw new Error('The operation was aborted.');
  }
}
