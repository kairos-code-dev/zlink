// SPDX-License-Identifier: MPL-2.0

import {
  RoutingId,
  SpotRouteBridgeEndpointCapabilities,
  type MessageLike,
  type RequestCallback,
  type RequestOperation,
  type SendOperation,
  type SpotRouteBridgeEndpointOptions
} from '../../../contracts';
import type { DealerSocket, RouterSocket } from '../../../contracts/sockets';
import { normalizeOperationPayload } from '../../buffers/message_conversion';
import { normalizeRoutingId } from '../../core/routing_id';
import { getNativeHandle, NativeHandle } from '../../handles/native_handle';
import { executeNativeRequest } from '../../messaging/request_executor';
import { startRequestProgress } from '../../messaging/request_progress';
import { requireNative } from '../../native/native';
import { validateCString } from '../../options/validation';
import { RuntimeRequestOperation, RuntimeSendOperation } from '../../sockets/socket_operation_builders';

function validateBridgeChannelName(value: string): string {
  const normalized = validateCString(value, 'channelName');
  if (normalized.length === 0) {
    throw new RangeError('channelName must not be empty');
  }
  return normalized;
}

function validatePublisherTopic(value: string): string {
  const normalized = validateCString(value, 'topic');
  if (normalized.length === 0) {
    throw new RangeError('topic must not be empty');
  }
  return normalized;
}

export class SpotRouteBridge extends NativeHandle {
  private readonly _endpointHandles = new Map<string, unknown>();

  constructor(ctx: NativeHandle, node: NativeHandle) {
    super(requireNative().spotRouteBridgeNew(getNativeHandle(ctx), getNativeHandle(node)));
  }

  attachDealerChannel(
    channelName: string,
    dealer: DealerSocket,
    options?: SpotRouteBridgeEndpointOptions
  ): void {
    const normalizedChannelName = validateBridgeChannelName(channelName);
    const dealerHandle = getNativeHandle(dealer as unknown as NativeHandle);
    requireNative().spotRouteBridgeAttachDealerChannel(
      this._native,
      normalizedChannelName,
      dealerHandle,
      normalizeEndpointCapabilities(options)
    );
    this._endpointHandles.set(normalizedChannelName, dealerHandle);
  }

  attachRouterChannel(
    channelName: string,
    router: RouterSocket,
    options?: SpotRouteBridgeEndpointOptions
  ): void {
    const normalizedChannelName = validateBridgeChannelName(channelName);
    const routerHandle = getNativeHandle(router as unknown as NativeHandle);
    requireNative().spotRouteBridgeAttachRouterChannel(
      this._native,
      normalizedChannelName,
      routerHandle,
      normalizeEndpointCapabilities(options)
    );
    this._endpointHandles.set(normalizedChannelName, routerHandle);
  }

  setTargetNode(channelName: string, targetNodeRid: RoutingId): void {
    requireNative().spotRouteBridgeSetTargetNode(
      this._native,
      validateBridgeChannelName(channelName),
      normalizeRoutingId(targetNodeRid)
    );
  }

  send(channelName: string, targetSpotRid: RoutingId): SendOperation {
    const normalizedChannelName = validateBridgeChannelName(channelName);
    const normalizedTargetSpotRid = normalizeRoutingId(targetSpotRid);
    return new RuntimeSendOperation((parts, flags) =>
      requireNative().spotRouteBridgeSend(
        this._native,
        normalizedChannelName,
        normalizedTargetSpotRid,
        normalizeOperationPayload(parts as MessageLike | readonly MessageLike[]),
        flags
      )
    );
  }

  request(channelName: string, targetSpotRid: RoutingId): RequestOperation {
    const normalizedChannelName = validateBridgeChannelName(channelName);
    const normalizedTargetSpotRid = normalizeRoutingId(targetSpotRid);
    return new RuntimeRequestOperation((parts, callbackOrTimeout, flagsOrTimeout, maybeTimeout) => {
      const normalizedParts = normalizeOperationPayload(parts as MessageLike | readonly MessageLike[]);
      return executeNativeRequest({
        callbackOrTimeout: callbackOrTimeout as RequestCallback | number | undefined,
        flagsOrTimeout,
        maybeTimeout,
        promiseTimeoutMayUseFlagsOrTimeout: true,
        startProgress: () => startRequestProgress(this.progressHandleFor(normalizedChannelName)),
        invoke: (callback, flags, timeoutMs) => {
          requireNative().spotRouteBridgeRequest(
            this._native,
            normalizedChannelName,
            normalizedTargetSpotRid,
            normalizedParts,
            callback,
            flags | 0,
            timeoutMs | 0
          );
        },
        submitErrorMessage: 'spot route bridge request failed',
        requestErrorMessage: 'spot route bridge request failed'
      });
    });
  }

  handleRouterReceived(
    channelName: string,
    sourceNodeRid: RoutingId,
    parts: MessageLike | readonly MessageLike[],
    requestSeq?: bigint | number
  ): boolean {
    const normalizedChannelName = validateBridgeChannelName(channelName);
    const normalizedSourceNodeRid = normalizeRoutingId(sourceNodeRid, 'sourceNodeRid');
    const normalizedParts = normalizeOperationPayload(parts);
    if (requestSeq === undefined) {
      return requireNative().spotRouteBridgeHandleRouterReceived(
        this._native,
        normalizedChannelName,
        normalizedSourceNodeRid,
        normalizedParts
      ) as boolean;
    }
    return requireNative().spotRouteBridgeHandleRouterReceivedWithMetadata(
      this._native,
      normalizedChannelName,
      normalizedSourceNodeRid,
      BigInt(requestSeq),
      normalizedParts
    ) as boolean;
  }

  drain(): number {
    return requireNative().spotRouteBridgeDrain(this._native) as number;
  }

  override close(): void {
    if (!this._native) return;
    requireNative().spotRouteBridgeClose(this._native);
    super.close();
  }

  private progressHandleFor(channelName: string): unknown {
    const handle = this._endpointHandles.get(channelName);
    if (!handle) {
      throw new TypeError(`spot route bridge channel is not attached: ${channelName}`);
    }
    return handle;
  }
}

function normalizeEndpointCapabilities(options?: SpotRouteBridgeEndpointOptions): number {
  return options?.capabilities === undefined
    ? SpotRouteBridgeEndpointCapabilities.RouteOnly
    : options.capabilities | 0;
}

export class SpotNodePublisher extends NativeHandle {
  constructor(node: NativeHandle) {
    super(requireNative().spotNodePublisherNew(getNativeHandle(node)));
  }

  publish(topic: string): SendOperation {
    const normalizedTopic = validatePublisherTopic(topic);
    return new RuntimeSendOperation((parts, flags) =>
      requireNative().spotNodePublisherPublish(
        this._native,
        normalizedTopic,
        normalizeOperationPayload(parts as MessageLike | readonly MessageLike[]),
        flags
      )
    );
  }

  override close(): void {
    if (!this._native) return;
    requireNative().spotNodePublisherClose(this._native);
    super.close();
  }
}
