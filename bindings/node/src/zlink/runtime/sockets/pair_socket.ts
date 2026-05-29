// SPDX-License-Identifier: MPL-2.0

import { CommonSocketOptions } from './socket_options';
import {
  MessageSocket,
} from './socket_operations';
import {
  NativeSocketType,
  type Context,
} from './socket_runtime_support';

export class PairSocket extends MessageSocket {
  readonly options: CommonSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.PAIR); this.options = CommonSocketOptions.create(this); }
}
