declare module '@bufbuild/protobuf' {
  export interface Message<TypeName extends string = string> {
    readonly $typeName: TypeName;
  }

  export interface MessageSchema<T extends Message = Message> {}

  export function fromBinary<T extends Message>(
    schema: MessageSchema<T>,
    bytes: Uint8Array
  ): T;

  export function toBinary<T extends Message>(
    schema: MessageSchema<T>,
    value: T
  ): Uint8Array;
}
