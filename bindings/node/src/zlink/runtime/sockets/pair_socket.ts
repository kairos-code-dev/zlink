// SPDX-License-Identifier: MPL-2.0

import { CommonSocketOptions } from './socket_options';
import {
  MessageSocket,
  NativeSocketType,
  type Context,
} from './socket_operations';

export class PairSocket extends MessageSocket {
  readonly options: CommonSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.PAIR); this.options = CommonSocketOptions.create(this); }
}
