import type { ZlinkStreamConnection } from '../Contracts';
import {
  ZlinkStreamErrorCode,
  ZlinkStreamMessageKind
} from '../Contracts';
import type { ZlinkStreamHeader } from '../Contracts/ZlinkStreamModels';
import type { ZlinkStreamFrameProtocol } from './Protocol/ZlinkStreamFrameProtocol';
import {
  ZLINK_STREAM_HEARTBEAT_PING,
  ZLINK_STREAM_HEARTBEAT_PONG
} from './Protocol/ZlinkStreamFrameProtocol';
import type { ZlinkStreamConnectorEvents } from './ZlinkStreamConnectorEvents';
import type { ZlinkStreamFrameSender } from './ZlinkStreamFrameSender';
import type { ZlinkStreamInboundObservers } from './ZlinkStreamInboundObservers';
import type { ZlinkStreamPendingRequests } from './ZlinkStreamPendingRequests';
import type { ZlinkStreamReceivedMessages } from './ZlinkStreamReceivedMessages';
import { connectorError, toStreamError, utf8Decode } from './ZlinkStreamSupport';
import { createInboundFlow } from './ZlinkFlowContext';
import { decodeSessionClosing, ZLINK_SESSION_CLOSING } from './Protocol/ZlinkSessionClosing';
import type { ZlinkStreamCloseReason } from '../Contracts';

export interface ZlinkStreamReceiveResult {
  readonly available: boolean;
  readonly inbound: boolean;
}

export class ZlinkStreamReceiveDispatcher {
  constructor(
    private readonly protocol: ZlinkStreamFrameProtocol,
    private readonly pendingRequests: ZlinkStreamPendingRequests,
    private readonly inboundObservers: ZlinkStreamInboundObservers,
    private readonly receivedMessages: ZlinkStreamReceivedMessages,
    private readonly frameSender: ZlinkStreamFrameSender,
    private readonly events: ZlinkStreamConnectorEvents,
    private readonly serverClosing?: (reason: ZlinkStreamCloseReason) => Promise<void>
  ) {}

  async readAndDispatch(
    connection: ZlinkStreamConnection | undefined,
    signal?: AbortSignal,
    isCurrent?: () => boolean
  ): Promise<ZlinkStreamReceiveResult> {
    if (connection?.read === undefined) {
      return { available: false, inbound: false };
    }
    const frameBytes = await connection.read(signal);
    if (isCurrent !== undefined && !isCurrent()) {
      return { available: false, inbound: false };
    }
    if (frameBytes === undefined) {
      return { available: false, inbound: false };
    }
    let frame: ReturnType<ZlinkStreamFrameProtocol['decode']>;
    try {
      frame = this.protocol.decode(frameBytes);
    } catch (cause) {
      await this.events.publishError(
        toStreamError(cause, ZlinkStreamErrorCode.FrameDecodeFailed, 'Frame decode failed.'),
        signal
      );
      return { available: true, inbound: false };
    }
    try {
      await this.dispatch(connection, frame.header, frame.payload, signal);
    } catch (cause) {
      if (
        frame.header.kind === ZlinkStreamMessageKind.Control &&
        frame.header.name === ZLINK_STREAM_HEARTBEAT_PING
      ) {
        throw cause;
      }
      await this.events.publishError(
        toStreamError(cause, ZlinkStreamErrorCode.FrameDecodeFailed, 'Frame dispatch failed.'),
        signal
      );
    }
    return { available: true, inbound: true };
  }

  private async dispatch(
    connection: ZlinkStreamConnection,
    header: ZlinkStreamHeader,
    payload: Uint8Array,
    signal?: AbortSignal
  ): Promise<void> {
    this.inboundObservers.enqueue(header, payload, signal);
    if (header.kind === ZlinkStreamMessageKind.Response && header.requestSeq !== undefined) {
      try {
        this.pendingRequests.resolve(header.requestSeq, {
          codec: header.codec,
          payload: this.protocol.decodePayload(header, payload)
        });
      } catch (cause) {
        this.pendingRequests.reject(
          header.requestSeq,
          toStreamError(cause, ZlinkStreamErrorCode.DecompressionFailed, 'Decompression failed.')
        );
      }
      return;
    }
    if (header.kind === ZlinkStreamMessageKind.Error && header.requestSeq !== undefined) {
      this.pendingRequests.reject(header.requestSeq, {
        code: ZlinkStreamErrorCode.RemoteError,
        message: utf8Decode(payload)
      });
      return;
    }
    if (header.kind === ZlinkStreamMessageKind.Error) {
      await this.events.publishError({
        code: ZlinkStreamErrorCode.RemoteError,
        message: utf8Decode(payload)
      }, signal);
      return;
    }
    if (header.kind === ZlinkStreamMessageKind.Control) {
      await this.dispatchControl(connection, header, payload, signal);
      return;
    }
    if (header.kind === ZlinkStreamMessageKind.Send) {
      const flow = createInboundFlow(header.flowId, header.flowOrigin);
      this.receivedMessages.enqueue({
        name: header.name,
        metadata: header.metadata,
        payload: { codec: header.codec, payload: this.protocol.decodePayload(header, payload) },
        flowId: flow.flowId,
        flowOrigin: flow.flowOrigin
      }, signal);
    }
  }

  private async dispatchControl(
    connection: ZlinkStreamConnection,
    header: ZlinkStreamHeader,
    payload: Uint8Array,
    signal?: AbortSignal
  ): Promise<void> {
    if (header.name === ZLINK_SESSION_CLOSING) {
      const closing = decodeSessionClosing(payload);
      await this.serverClosing?.(closing.closeReason);
      return;
    }
    if (payload.length !== 0) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Control packet payload must be empty.');
    }
    if (header.name === ZLINK_STREAM_HEARTBEAT_PING) {
      try {
        await this.frameSender.sendControl(connection, ZLINK_STREAM_HEARTBEAT_PONG, signal);
      } catch (cause) {
        throw connectorError(
          ZlinkStreamErrorCode.SendFailed,
          cause instanceof Error ? cause.message : 'Heartbeat pong send failed.'
        );
      }
      return;
    }
    if (header.name !== ZLINK_STREAM_HEARTBEAT_PONG) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Unknown control packet.');
    }
  }
}
