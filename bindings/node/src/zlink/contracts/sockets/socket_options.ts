// SPDX-License-Identifier: MPL-2.0

import type { Message, MessageLike } from '../messaging';
import type { RoutingId } from '../core';
import type { RidDuplicatePolicyValue } from './socket_constants';

export interface CommonSocketOptions {
  linger: number;
  sendHwm: number;
  recvHwm: number;
  sendTimeout: number;
  recvTimeout: number;
  immediate: boolean;
  ridDuplicatePolicy: RidDuplicatePolicyValue;
  connectTimeout: number;
  ipv6: boolean;
  tcpNoDelay: boolean;
  tcpKeepalive: number;
  heartbeatInterval: number;
  heartbeatTtl: number;
  heartbeatTimeout: number;
  maxMsgSize: bigint;
  readonly lastEndpoint: string;
  backlog: number;
  reconnectInterval: number;
  reconnectIntervalMax: number;
  submitRetryMode: number;
  submitRetryTimeout: number;
  submitRetryAttempts: number;
}

export interface DealerSocketOptions extends CommonSocketOptions {
  probe: boolean;
  requestTimeout: number;
  peerWeight: number;
}

export interface RouterSocketOptions extends CommonSocketOptions {
  mandatory: boolean;
  handover: boolean;
  probe: boolean;
  readonly connectRoutingId: RoutingId | null;
  setConnectRoutingId(routingId: RoutingId): void;
  requestTimeout: number;
  peerWeight: number;
}

export interface StreamSocketOptions extends CommonSocketOptions {
  notify: boolean;
}

export interface PubSocketOptions extends CommonSocketOptions {
  verbose: boolean;
  verboser: boolean;
  noDrop: boolean;
  manual: boolean;
  manualLastValue: boolean;
  readonly topicsCount: number;
  welcomeMessage(): Message;
  setWelcomeMessage(message: MessageLike): void;
  approveSubscribe(routingId: RoutingId): void;
  rejectSubscribe(routingId: RoutingId): void;
}

export interface SubSocketOptions extends CommonSocketOptions {
  readonly topicsCount: number;
}
