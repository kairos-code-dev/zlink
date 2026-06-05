/* SPDX-License-Identifier: MPL-2.0 */

#if __has_include("CoreMinimal.h")

#include "ZLinkStreamConnector.h"

#include "IPAddress.h"
#include "Misc/AutomationTest.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{

struct FLoopbackServer
{
    FSocket *ListenSocket = nullptr;
    FSocket *ClientSocket = nullptr;
    TSharedPtr<FInternetAddr> Address;
    FString Endpoint;

    ~FLoopbackServer () { Close (); }

    bool Start ()
    {
        ISocketSubsystem *SocketSubsystem = ISocketSubsystem::Get (PLATFORM_SOCKETSUBSYSTEM);
        if (!SocketSubsystem) {
            return false;
        }

        ListenSocket =
          SocketSubsystem->CreateSocket (NAME_Stream, TEXT ("ZLinkStreamConnectorAutomationServer"), false);
        if (!ListenSocket) {
            return false;
        }

        Address = SocketSubsystem->CreateInternetAddr ();
        bool bValidAddress = false;
        Address->SetIp (TEXT ("127.0.0.1"), bValidAddress);
        Address->SetPort (0);
        if (!bValidAddress || !ListenSocket->Bind (*Address) || !ListenSocket->Listen (8)) {
            return false;
        }

        TSharedRef<FInternetAddr> BoundAddress = SocketSubsystem->CreateInternetAddr ();
        ListenSocket->GetAddress (*BoundAddress);
        Endpoint = FString::Printf (TEXT ("tcp://127.0.0.1:%d"), BoundAddress->GetPort ());
        return true;
    }

    bool Accept ()
    {
        if (!ListenSocket) {
            return false;
        }
        ClientSocket = ListenSocket->Accept (TEXT ("ZLinkStreamConnectorAutomationClient"));
        return ClientSocket != nullptr;
    }

    static uint16 ReadU16 (const TArray<uint8> &Bytes, int32 Offset)
    {
        return static_cast<uint16> ((static_cast<uint16> (Bytes[Offset]) << 8)
                                    | static_cast<uint16> (Bytes[Offset + 1]));
    }

    static uint32 ReadU32 (const TArray<uint8> &Bytes, int32 Offset)
    {
        return (static_cast<uint32> (Bytes[Offset]) << 24) | (static_cast<uint32> (Bytes[Offset + 1]) << 16)
               | (static_cast<uint32> (Bytes[Offset + 2]) << 8) | static_cast<uint32> (Bytes[Offset + 3]);
    }

    static uint64 ReadU64 (const TArray<uint8> &Bytes, int32 &Offset)
    {
        uint64 Value = 0;
        for (int32 Index = 0; Index < 8; ++Index) {
            Value = (Value << 8) | Bytes[Offset + Index];
        }
        Offset += 8;
        return Value;
    }

    static void AppendU16 (TArray<uint8> &Bytes, uint16 Value)
    {
        Bytes.Add (static_cast<uint8> ((Value >> 8) & 0xff));
        Bytes.Add (static_cast<uint8> (Value & 0xff));
    }

    static void AppendU32 (TArray<uint8> &Bytes, uint32 Value)
    {
        Bytes.Add (static_cast<uint8> ((Value >> 24) & 0xff));
        Bytes.Add (static_cast<uint8> ((Value >> 16) & 0xff));
        Bytes.Add (static_cast<uint8> ((Value >> 8) & 0xff));
        Bytes.Add (static_cast<uint8> (Value & 0xff));
    }

    static void AppendLz4Length (TArray<uint8> &Bytes, int32 Value)
    {
        while (Value >= 255) {
            Bytes.Add (255);
            Value -= 255;
        }
        Bytes.Add (static_cast<uint8> (Value));
    }

    static TArray<uint8> EncodeLz4Payload (const TArray<uint8> &Payload)
    {
        TArray<uint8> Output;
        AppendU32 (Output, static_cast<uint32> (Payload.Num ()));
        if (Payload.IsEmpty ()) {
            return Output;
        }
        Output.Add (static_cast<uint8> (FMath::Min (Payload.Num (), 15) << 4));
        if (Payload.Num () >= 15) {
            AppendLz4Length (Output, Payload.Num () - 15);
        }
        Output.Append (Payload);
        return Output;
    }

    static bool DecodeLz4Payload (const TArray<uint8> &Payload, TArray<uint8> &Output)
    {
        Output.Empty ();
        if (Payload.Num () < 4) {
            return false;
        }
        const uint32 OriginalSize = ReadU32 (Payload, 0);
        if (OriginalSize == 0) {
            return true;
        }
        int32 Offset = 4;
        while (Offset < Payload.Num ()) {
            const uint8 Token = Payload[Offset++];
            int32 LiteralSize = Token >> 4;
            if (LiteralSize == 15) {
                while (Offset < Payload.Num () && Payload[Offset] == 255) {
                    LiteralSize += 255;
                    ++Offset;
                }
                if (Offset >= Payload.Num ()) {
                    return false;
                }
                LiteralSize += Payload[Offset++];
            }
            if (Payload.Num () - Offset < LiteralSize) {
                return false;
            }
            Output.Append (Payload.GetData () + Offset, LiteralSize);
            Offset += LiteralSize;
            if (Offset == Payload.Num ()) {
                break;
            }
            if (Payload.Num () - Offset < 2) {
                return false;
            }
            const int32 MatchOffset =
              static_cast<int32> (Payload[Offset]) | (static_cast<int32> (Payload[Offset + 1]) << 8);
            Offset += 2;
            if (MatchOffset <= 0 || MatchOffset > Output.Num ()) {
                return false;
            }
            int32 MatchSize = (Token & 0x0f) + 4;
            if ((Token & 0x0f) == 15) {
                while (Offset < Payload.Num () && Payload[Offset] == 255) {
                    MatchSize += 255;
                    ++Offset;
                }
                if (Offset >= Payload.Num ()) {
                    return false;
                }
                MatchSize += Payload[Offset++];
            }
            for (int32 Index = 0; Index < MatchSize; ++Index) {
                Output.Add (Output[Output.Num () - MatchOffset]);
            }
            if (Output.Num () > static_cast<int32> (OriginalSize)) {
                return false;
            }
        }
        return Output.Num () == static_cast<int32> (OriginalSize);
    }

    bool ReadExact (int32 Count, TArray<uint8> &Output)
    {
        Output.SetNumUninitialized (Count);
        int32 Offset = 0;
        while (ClientSocket && Offset < Count) {
            int32 BytesRead = 0;
            if (!ClientSocket->Recv (Output.GetData () + Offset, Count - Offset, BytesRead) || BytesRead <= 0) {
                return false;
            }
            Offset += BytesRead;
        }
        return Offset == Count;
    }

    bool ReadFrame (uint8 &Kind,
                    uint8 &Codec,
                    uint8 &Flags,
                    uint64 &RequestSeq,
                    FName &PacketName,
                    TMap<FString, FString> &Metadata,
                    FString &Payload)
    {
        TArray<uint8> Prefix;
        if (!ReadExact (6, Prefix)) {
            return false;
        }
        const uint16 HeaderSize = ReadU16 (Prefix, 0);
        const uint32 PayloadSize = ReadU32 (Prefix, 2);
        TArray<uint8> Header;
        TArray<uint8> PayloadBytes;
        if (!ReadExact (HeaderSize, Header) || !ReadExact (static_cast<int32> (PayloadSize), PayloadBytes)) {
            return false;
        }
        int32 Offset = 0;
        Kind = Header[Offset++];
        Codec = Header[Offset++];
        Flags = Header[Offset++];
        RequestSeq = 0;
        if ((Flags & 0x01) != 0) {
            RequestSeq = ReadU64 (Header, Offset);
        }
        const uint8 NameSize = Header[Offset++];
        FString Name;
        for (uint8 Index = 0; Index < NameSize; ++Index) {
            Name.AppendChar (static_cast<TCHAR> (Header[Offset + Index]));
        }
        Offset += NameSize;
        PacketName = FName (*Name);
        Metadata.Empty ();
        if ((Flags & 0x02) != 0) {
            const uint16 MetadataSize = ReadU16 (Header, Offset);
            Offset += 2;
            const int32 MetadataEnd = Offset + MetadataSize;
            const uint8 MetadataCount = Header[Offset++];
            for (uint8 Index = 0; Index < MetadataCount; ++Index) {
                const uint8 KeySize = Header[Offset++];
                FString Key;
                for (uint8 KeyIndex = 0; KeyIndex < KeySize; ++KeyIndex) {
                    Key.AppendChar (static_cast<TCHAR> (Header[Offset + KeyIndex]));
                }
                Offset += KeySize;
                const uint16 ValueSize = ReadU16 (Header, Offset);
                Offset += 2;
                FString Value;
                for (uint16 ValueIndex = 0; ValueIndex < ValueSize; ++ValueIndex) {
                    Value.AppendChar (static_cast<TCHAR> (Header[Offset + ValueIndex]));
                }
                Offset += ValueSize;
                Metadata.Add (Key, Value);
            }
            if (Offset != MetadataEnd) {
                return false;
            }
        }
        if ((Flags & 0x04) != 0) {
            TArray<uint8> Decompressed;
            if (!DecodeLz4Payload (PayloadBytes, Decompressed)) {
                return false;
            }
            PayloadBytes = MoveTemp (Decompressed);
        }
        const FUTF8ToTCHAR Converted (reinterpret_cast<const ANSICHAR *> (PayloadBytes.GetData ()),
                                      PayloadBytes.Num ());
        Payload = FString (Converted.Length (), Converted.Get ());
        return true;
    }

    bool SendFrame (uint8 Kind,
                    uint64 RequestSeq,
                    const FString &PacketName,
                    const FString &Payload,
                    const TMap<FString, FString> &Metadata,
                    bool bCompressed = false)
    {
        if (!ClientSocket) {
            return false;
        }
        TArray<uint8> Header;
        Header.Add (Kind);
        Header.Add (1);
        uint8 Flags = Metadata.IsEmpty () ? 0 : 0x02;
        if (RequestSeq != 0) {
            Flags |= 0x01;
        }
        if (bCompressed) {
            Flags |= 0x04;
        }
        Header.Add (Flags);
        if (RequestSeq != 0) {
            for (int32 Shift = 56; Shift >= 0; Shift -= 8) {
                Header.Add (static_cast<uint8> ((RequestSeq >> Shift) & 0xff));
            }
        }
        const FTCHARToUTF8 NameBytes (*PacketName);
        Header.Add (static_cast<uint8> (NameBytes.Length ()));
        Header.Append (reinterpret_cast<const uint8 *> (NameBytes.Get ()), NameBytes.Length ());
        if (!Metadata.IsEmpty ()) {
            TArray<uint8> MetadataBytes;
            MetadataBytes.Add (static_cast<uint8> (Metadata.Num ()));
            for (const auto &Entry : Metadata) {
                const FTCHARToUTF8 KeyBytes (*Entry.Key);
                const FTCHARToUTF8 ValueBytes (*Entry.Value);
                MetadataBytes.Add (static_cast<uint8> (KeyBytes.Length ()));
                MetadataBytes.Append (reinterpret_cast<const uint8 *> (KeyBytes.Get ()), KeyBytes.Length ());
                AppendU16 (MetadataBytes, static_cast<uint16> (ValueBytes.Length ()));
                MetadataBytes.Append (reinterpret_cast<const uint8 *> (ValueBytes.Get ()), ValueBytes.Length ());
            }
            AppendU16 (Header, static_cast<uint16> (MetadataBytes.Num ()));
            Header.Append (MetadataBytes);
        }

        const FTCHARToUTF8 PayloadText (*Payload);
        TArray<uint8> PayloadBytes;
        PayloadBytes.Append (reinterpret_cast<const uint8 *> (PayloadText.Get ()), PayloadText.Length ());
        if (bCompressed) {
            PayloadBytes = EncodeLz4Payload (PayloadBytes);
        }
        TArray<uint8> Frame;
        AppendU16 (Frame, static_cast<uint16> (Header.Num ()));
        AppendU32 (Frame, static_cast<uint32> (PayloadBytes.Num ()));
        Frame.Append (Header);
        Frame.Append (PayloadBytes);

        int32 BytesSent = 0;
        return ClientSocket->Send (Frame.GetData (), Frame.Num (), BytesSent) && BytesSent == Frame.Num ();
    }

    bool SendPushFrame (const FString &PacketName,
                        const FString &Payload,
                        const TMap<FString, FString> &Metadata,
                        bool bCompressed = false)
    {
        return SendFrame (1, 0, PacketName, Payload, Metadata, bCompressed);
    }

    bool SendResponseFrame (uint64 RequestSeq,
                            const FString &PacketName,
                            const FString &Payload,
                            const TMap<FString, FString> &Metadata,
                            bool bCompressed = false)
    {
        return SendFrame (3, RequestSeq, PacketName, Payload, Metadata, bCompressed);
    }

    void Close ()
    {
        ISocketSubsystem *SocketSubsystem = ISocketSubsystem::Get (PLATFORM_SOCKETSUBSYSTEM);
        if (ClientSocket) {
            ClientSocket->Close ();
            if (SocketSubsystem) {
                SocketSubsystem->DestroySocket (ClientSocket);
            }
            ClientSocket = nullptr;
        }
        if (ListenSocket) {
            ListenSocket->Close ();
            if (SocketSubsystem) {
                SocketSubsystem->DestroySocket (ListenSocket);
            }
            ListenSocket = nullptr;
        }
    }
};

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST (FZLinkStreamConnectorLoopbackTest,
                                  "ZLink.StreamConnector.Loopback",
                                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FZLinkStreamConnectorLoopbackTest::RunTest (const FString &Parameters)
{
    (void) Parameters;

    FLoopbackServer Server;
    TestTrue (TEXT ("loopback server starts"), Server.Start ());

    UZLinkStreamConnector *Connector = NewObject<UZLinkStreamConnector> ();
    bool bPacketReceived = false;
    FName ReceivedPacketName;
    FString ReceivedTraceId;
    bool bReceivedCompressed = false;
    bool bRequestCompleted = false;
    FName CompletedPacketName;
    FString CompletedTraceId;
    bool bCompletedCompressed = false;
    Connector->OnPacketReceivedNative.AddLambda ([&bPacketReceived, &ReceivedPacketName, &ReceivedTraceId,
                                                  &bReceivedCompressed] (const FZLinkStreamPacket &Packet) {
        bPacketReceived = true;
        ReceivedPacketName = Packet.PacketName;
        bReceivedCompressed = Packet.bCompressed;
        if (const FString *TraceId = Packet.Metadata.Find (TEXT ("traceId"))) {
            ReceivedTraceId = *TraceId;
        }
    });
    Connector->OnRequestCompletedNative.AddLambda ([&bRequestCompleted, &CompletedPacketName, &CompletedTraceId,
                                                    &bCompletedCompressed] (const FZLinkStreamPacket &Packet) {
        bRequestCompleted = true;
        CompletedPacketName = Packet.PacketName;
        bCompletedCompressed = Packet.bCompressed;
        if (const FString *TraceId = Packet.Metadata.Find (TEXT ("traceId"))) {
            CompletedTraceId = *TraceId;
        }
    });

    Connector->Connect (Server.Endpoint);
    TestTrue (TEXT ("server accepts connector"), Server.Accept ());
    TestTrue (TEXT ("connector connected"), Connector->IsConnected ());

    Connector->SendJson (TEXT ("chat.send"), TEXT ("{\"text\":\"hello\"}"));
    uint8 SendKind = 0;
    uint8 SendCodec = 0;
    uint8 SendFlags = 0;
    uint64 SendSeq = 0;
    FName SendPacketName;
    TMap<FString, FString> SendMetadata;
    FString SendPayload;
    TestTrue (TEXT ("server receives send packet"),
              Server.ReadFrame (SendKind, SendCodec, SendFlags, SendSeq, SendPacketName, SendMetadata, SendPayload));
    TestEqual (TEXT ("send kind"), SendKind, static_cast<uint8> (1));
    TestEqual (TEXT ("send codec"), SendCodec, static_cast<uint8> (1));
    TestEqual (TEXT ("send flags"), SendFlags, static_cast<uint8> (0));
    TestEqual (TEXT ("send packet name"), SendPacketName, FName (TEXT ("chat.send")));
    TestEqual (TEXT ("send payload"), SendPayload, FString (TEXT ("{\"text\":\"hello\"}")));

    FZLinkStreamSendOptions SendOptions;
    SendOptions.Metadata.Add (TEXT ("traceId"), TEXT ("send-1"));
    SendOptions.bCompress = true;
    Connector->SendJsonWithOptions (TEXT ("chat.send.metadata"), TEXT ("{\"text\":\"hello\"}"), SendOptions);
    TestTrue (TEXT ("server receives metadata send packet"),
              Server.ReadFrame (SendKind, SendCodec, SendFlags, SendSeq, SendPacketName, SendMetadata, SendPayload));
    TestEqual (TEXT ("metadata send flags"), SendFlags, static_cast<uint8> (6));
    TestEqual (TEXT ("metadata send trace id"), SendMetadata[TEXT ("traceId")], FString (TEXT ("send-1")));
    TestEqual (TEXT ("metadata send compressed payload"), SendPayload, FString (TEXT ("{\"text\":\"hello\"}")));

    Connector->RequestJson (TEXT ("chat.request"), TEXT ("{\"text\":\"hello\"}"), 1.0f);
    uint8 RequestKind = 0;
    uint8 RequestCodec = 0;
    uint8 RequestFlags = 0;
    uint64 RequestSeq = 0;
    FName RequestPacketName;
    TMap<FString, FString> RequestMetadata;
    FString RequestPayload;
    TestTrue (TEXT ("server receives request packet"),
              Server.ReadFrame (RequestKind, RequestCodec, RequestFlags, RequestSeq, RequestPacketName, RequestMetadata,
                                RequestPayload));
    TestEqual (TEXT ("request kind"), RequestKind, static_cast<uint8> (2));
    TestEqual (TEXT ("request codec"), RequestCodec, static_cast<uint8> (1));
    TestEqual (TEXT ("request flags"), RequestFlags, static_cast<uint8> (1));
    TestTrue (TEXT ("request seq assigned"), RequestSeq != 0);
    TestEqual (TEXT ("request packet name"), RequestPacketName, FName (TEXT ("chat.request")));

    TMap<FString, FString> ResponseMetadata;
    ResponseMetadata.Add (TEXT ("traceId"), TEXT ("reply-1"));
    TestTrue (TEXT ("server sends response"),
              Server.SendResponseFrame (RequestSeq, TEXT ("reply"), TEXT ("{\"ok\":true}"), ResponseMetadata, true));
    TestEqual (TEXT ("pending response count before dispatch"), Connector->PendingDispatchCount (), 1);
    TestFalse (TEXT ("response waits for dispatch"), bRequestCompleted);
    Connector->Dispatch ();
    TestTrue (TEXT ("native callback receives response"), bRequestCompleted);
    TestEqual (TEXT ("pending response count after dispatch"), Connector->PendingDispatchCount (), 0);
    TestEqual (TEXT ("completed packet name"), CompletedPacketName, FName (TEXT ("reply")));
    TestEqual (TEXT ("completed trace id"), CompletedTraceId, FString (TEXT ("reply-1")));
    TestTrue (TEXT ("completed packet is compressed"), bCompletedCompressed);

    FZLinkStreamSendOptions RequestOptions;
    RequestOptions.Metadata.Add (TEXT ("traceId"), TEXT ("request-1"));
    RequestOptions.bCompress = true;
    Connector->RequestJsonWithOptions (TEXT ("chat.request.metadata"), TEXT ("{\"text\":\"hello\"}"), 1.0f,
                                       RequestOptions);
    TestTrue (TEXT ("server receives metadata request packet"),
              Server.ReadFrame (RequestKind, RequestCodec, RequestFlags, RequestSeq, RequestPacketName, RequestMetadata,
                                RequestPayload));
    TestEqual (TEXT ("metadata request flags"), RequestFlags, static_cast<uint8> (7));
    TestEqual (TEXT ("metadata request trace id"), RequestMetadata[TEXT ("traceId")], FString (TEXT ("request-1")));
    TestEqual (TEXT ("metadata request compressed payload"), RequestPayload, FString (TEXT ("{\"text\":\"hello\"}")));

    TMap<FString, FString> PushMetadata;
    PushMetadata.Add (TEXT ("traceId"), TEXT ("push-1"));
    TestTrue (TEXT ("server sends push"),
              Server.SendPushFrame (TEXT ("ServerNotify"), TEXT ("{}"), PushMetadata, true));
    TestEqual (TEXT ("pending push count before dispatch"), Connector->PendingDispatchCount (), 1);
    TestFalse (TEXT ("push waits for dispatch"), bPacketReceived);
    Connector->Dispatch ();
    TestTrue (TEXT ("native callback receives push"), bPacketReceived);
    TestEqual (TEXT ("pending push count after dispatch"), Connector->PendingDispatchCount (), 0);
    TestEqual (TEXT ("received packet name"), ReceivedPacketName, FName (TEXT ("ServerNotify")));
    TestEqual (TEXT ("received trace id"), ReceivedTraceId, FString (TEXT ("push-1")));
    TestTrue (TEXT ("received push is compressed"), bReceivedCompressed);

    Connector->ShutdownForPie ();
    TestFalse (TEXT ("connector closed"), Connector->IsConnected ());
    Server.Close ();
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
#endif // __has_include("CoreMinimal.h")
