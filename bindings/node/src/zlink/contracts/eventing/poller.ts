// SPDX-License-Identifier: MPL-2.0

import type { BaseSocket } from '../sockets';
import type { PollEventFlagValue } from '../sockets/socket_constants';
import type { Timer } from './timer';

export interface PollEvent {
  readonly sourceKind: number;
  readonly slot: number;
  readonly revents: number;
  readonly fd: number;
}

export interface PollEvents {
  readonly capacity: number;
  readonly readyCount: number;
  sourceKind(index: number): number;
  slot(index: number): number;
  revents(index: number): number;
  fd(index: number): number;
  hasEvent(index: number, event: PollEventFlagValue): boolean;
  close(): void;
}

export interface Poller {
  readonly size: number;
  add(socket: BaseSocket, events: readonly PollEventFlagValue[], slot: number): void;
  add(timer: Timer, slot: number): void;
  modify(socket: BaseSocket, events: readonly PollEventFlagValue[]): void;
  remove(socket: BaseSocket): boolean;
  remove(timer: Timer): boolean;
  addFd(fd: number, events: readonly PollEventFlagValue[], slot: number): void;
  modifyFd(fd: number, events: readonly PollEventFlagValue[]): void;
  removeFd(fd: number): boolean;
  wait(events: PollEvents, timeoutMs: number): number;
  close(): void;
}
