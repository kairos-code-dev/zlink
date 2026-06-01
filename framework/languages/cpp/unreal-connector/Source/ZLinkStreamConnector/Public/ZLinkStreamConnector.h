/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#if __has_include("CoreMinimal.h")
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ZLinkStreamConnector.generated.h"
#else
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define UCLASS(...)
#define UFUNCTION(...)
#define UPROPERTY(...)
#define GENERATED_BODY()
#define BlueprintType
#define BlueprintCallable
#define BlueprintAssignable
class UObject
{
};
using FString = std::string;
using FName = std::string;
template<typename T>
using TArray = std::vector<T>;
template<typename K, typename V>
using TMap = std::map<K, V>;
#endif

#if __has_include("CoreMinimal.h")
#include <memory>
#endif

enum class EZLinkStreamConnectionState : std::uint8_t
{
  Created,
  Connecting,
  Connected,
  Reconnecting,
  Disconnected,
  Closed
};

struct FZLinkStreamPacket
{
  FName PacketName;
  TArray<std::uint8_t> Payload;
  TMap<FString, FString> Metadata;
  bool bCompressed = false;
};

namespace UE::ZLinkStreamConnector::Private
{
class FZLinkStreamConnectorRuntime;
}

UCLASS(BlueprintType)
class UZLinkStreamConnector : public UObject
{
  GENERATED_BODY()

public:
  UZLinkStreamConnector ();
  ~UZLinkStreamConnector ();

  UFUNCTION(BlueprintCallable)
  void Connect (const FString &Endpoint);

  UFUNCTION(BlueprintCallable)
  void Close ();

  UFUNCTION(BlueprintCallable)
  void SendJson (FName PacketName, const FString &JsonPayload);

  UFUNCTION(BlueprintCallable)
  void RequestJson (FName PacketName,
                    const FString &JsonPayload,
                    float TimeoutSeconds);

  UFUNCTION(BlueprintCallable)
  void Dispatch ();

  UFUNCTION(BlueprintCallable)
  void Tick (float DeltaSeconds);

  UFUNCTION(BlueprintCallable)
  void ShutdownForPie ();

  UFUNCTION(BlueprintCallable)
  void ShutdownForMapUnload ();

  UFUNCTION(BlueprintCallable)
  void ShutdownForGameInstanceShutdown ();

  bool IsConnected () const;
  int PendingDispatchCount () const;
  EZLinkStreamConnectionState LastState () const;

private:
  std::unique_ptr<UE::ZLinkStreamConnector::Private::FZLinkStreamConnectorRuntime>
    _runtime;
};
