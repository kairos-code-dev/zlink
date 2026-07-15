import { zlinkStreamAssert } from '@zlink-systems/stream-connector';

export const ensure: (condition: boolean, message: string) => asserts condition = zlinkStreamAssert.ensure;
