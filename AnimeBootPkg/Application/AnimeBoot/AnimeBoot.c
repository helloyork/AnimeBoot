#include "AnimeBoot.h"
#include "DisplayMath.h"
#include "GopBlitter.h"

#include <Guid/FileInfo.h>
#include <IndustryStandard/Bmp.h>
#include <Library/AsciiLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FileHandleLib.h>

#define DEFAULT_PACKAGE_PATH        L"\\EFI\\AnimeBoot\\splash.anim"
#define DEFAULT_MANIFEST_PATH       L"\\EFI\\AnimeBoot\\sequence.anim.json"
#define DEFAULT_NEXT_STAGE_PATH     L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"

#define AB_DEFAULT_FPS              24
#define AB_DEFAULT_FRAME_DURATION   (1000000U / AB_DEFAULT_FPS)
#define AB_DEFAULT_MAX_MEMORY_BYTES (64ULL * 1024ULL * 1024ULL)
#define AB_MAX_FRAME_DIMENSION      1920U
#define AB_MAX_FRAME_COUNT          4096U
#define AB_MAX_FRAME_SIZE_BYTES     (16U * 1024U * 1024U)
#define AB_MAX_LOOP_COUNT           100U
#define AB_MIN_FRAME_DURATION_US    10000U

typedef struct {
  UINT32 LogicalWidth;
  UINT32 LogicalHeight;
  UINT32 FrameDurationUs;
  UINT32 LoopCount;
  BOOLEAN AllowKeySkip;
  UINT64 MaxMemoryBytes;
  UINT32 MaxTotalDurationMs;
  CHAR8  Scaling[16];
} PLAYBACK_CONFIG;

typedef struct {
  EFI_FILE_PROTOCOL    *Handle;
  UINT64               FileSize;
  ANIM_PACKAGE_HEADER  Header;
  ANIM_FRAME_DESC      *FrameTable;
  CHAR8                *ManifestJson;
  UINT32               ManifestSize;
} ANIM_PACKAGE_STATE;

typedef struct {
  CHAR16 *Path;
  UINT32 DurationUs;
} LOOSE_FRAME_DESC;

typedef struct {
  PLAYBACK_CONFIG  Config;
  LOOSE_FRAME_DESC *Frames;
  UINT32           FrameCount;
} LOOSE_MANIFEST_STATE;

typedef struct {
  ANIM_PACKAGE_STATE *Package;
} PACKAGE_PLAYBACK_CONTEXT;

typedef struct {
  const LOOSE_MANIFEST_STATE *Manifest;
  EFI_FILE_PROTOCOL          *Root;
} LOOSE_PLAYBACK_CONTEXT;

typedef EFI_STATUS (*FRAME_LOADER)(
    VOID *Context,
    UINT32 FrameIndex,
    FRAME_BUFFER *Target,
    UINT32 *DurationUs
    );

static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *mTextInputEx = NULL;

static EFI_STATUS
AbOpenRoot(
    EFI_HANDLE ImageHandle,
    EFI_FILE_PROTOCOL **Root,
    EFI_LOADED_IMAGE_PROTOCOL **LoadedImage);

static EFI_STATUS
AbOpenRootFromPath(
    CONST CHAR16 *PathSpec,
    EFI_FILE_PROTOCOL **Root);

static EFI_STATUS
AbEnumerateFatPartitions(
    EFI_HANDLE **Handles,
    UINTN *HandleCount);

static EFI_STATUS
AbFindPartitionByLabel(
    CONST CHAR16 *Label,
    EFI_HANDLE *Handle);

static EFI_STATUS
AbGetPartitionLabel(
    EFI_HANDLE Handle,
    CHAR16 *Label,
    UINTN LabelSize);

static EFI_STATUS
AbParsePathSpec(
    CONST CHAR16 *PathSpec,
    CHAR16 *PartitionSpec,
    UINTN PartitionSpecSize,
    CHAR16 *FilePath,
    UINTN FilePathSize);

static EFI_STATUS
AbLoadAnimationConfig(
    EFI_FILE_PROTOCOL *Root,
    ANIMATION_CONFIG *Config);

static VOID
AbFreeAnimationConfig(ANIMATION_CONFIG *Config);

static EFI_STATUS
AbPlayFromPackage(
    EFI_FILE_PROTOCOL *Root,
    GOP_STATE *GopState);

static EFI_STATUS
AbPlayFromLoose(
    EFI_FILE_PROTOCOL *Root,
    GOP_STATE *GopState);

static EFI_STATUS
AbRunPlayback(
    UINT32 FrameCount,
    PLAYBACK_CONFIG *Config,
    GOP_STATE *GopState,
    FRAME_LOADER Loader,
    VOID *Context);

static EFI_STATUS
AbLoadPackageFromPath(
    EFI_FILE_PROTOCOL *Root,
    CONST CHAR16 *Path,
    ANIM_PACKAGE_STATE *Package);

static VOID
AbClosePackage(ANIM_PACKAGE_STATE *Package);

static EFI_STATUS
AbLoadLooseManifest(
    EFI_FILE_PROTOCOL *Root,
    CONST CHAR16 *Path,
    LOOSE_MANIFEST_STATE *Manifest);

static VOID
AbFreeLooseManifest(LOOSE_MANIFEST_STATE *Manifest);

static EFI_STATUS
AbPackageFrameLoader(
    VOID *Context,
    UINT32 FrameIndex,
    FRAME_BUFFER *Target,
    UINT32 *DurationUs);

static EFI_STATUS
AbLooseFrameLoader(
    VOID *Context,
    UINT32 FrameIndex,
    FRAME_BUFFER *Target,
    UINT32 *DurationUs);

static EFI_STATUS
AbDecodeFramePayload(
    CONST UINT8 *Payload,
    UINTN PayloadSize,
    ANIM_PIXEL_FORMAT FormatHint,
    FRAME_BUFFER *Target);

static EFI_STATUS
AbDecodeBmpPayload(
    CONST UINT8 *Payload,
    UINTN PayloadSize,
    FRAME_BUFFER *Target);

static EFI_STATUS
AbDecodeRawPayload(
    CONST UINT8 *Payload,
    UINTN PayloadSize,
    FRAME_BUFFER *Target);

static EFI_STATUS
AbReadFileChunk(
    EFI_FILE_PROTOCOL *File,
    UINT64 Offset,
    VOID *Buffer,
    UINTN Length);

static VOID
AbInitPlaybackDefaults(PLAYBACK_CONFIG *Config);

static VOID
AbInitPlaybackFromHeader(
    const ANIM_PACKAGE_HEADER *Header,
    PLAYBACK_CONFIG *Config);

static VOID
AbApplyManifestOverrides(
    CHAR8 *Json,
    PLAYBACK_CONFIG *Config);

static CHAR8 *
AbJsonFindKey(
    CHAR8 *Json,
    CONST CHAR8 *Key);

static CHAR8 *
AbSkipJsonNoise(CHAR8 *Ptr);

static BOOLEAN
AbJsonReadUint(
    CHAR8 *Json,
    CONST CHAR8 *Key,
    UINT64 *Value);

static BOOLEAN
AbJsonReadBool(
    CHAR8 *Json,
    CONST CHAR8 *Key,
    BOOLEAN *Value);

static BOOLEAN
AbJsonReadString(
    CHAR8 *Json,
    CONST CHAR8 *Key,
    CHAR8 *Buffer,
    UINTN BufferSize);

static EFI_STATUS
AbParseLooseFrames(
    CHAR8 *Json,
    CONST CHAR16 *ManifestPath,
    LOOSE_MANIFEST_STATE *Manifest);

static EFI_STATUS
AbParseFrameObject(
    CHAR8 *ObjectJson,
    CONST CHAR16 *BaseDirectory,
    LOOSE_FRAME_DESC *Frame);

static CHAR16 *
AbDuplicateString(CONST CHAR16 *Source);

static CHAR16 *
AbExtractDirectory(CONST CHAR16 *Path);

static VOID
AbNormalizePathSeparators(CHAR16 *Path);

static CHAR16 *
AbJoinPaths(CONST CHAR16 *Base, CONST CHAR16 *Child);

static CHAR16 *
AbAsciiPathToUnicode(CONST CHAR8 *Path);

static BOOLEAN
AbUserRequestedSkip(BOOLEAN AllowSkip);

static VOID
AbInitInputProtocols(VOID);

static VOID
AbFlushKeys(VOID);

static EFI_STATUS
AbLaunchNextStage(
    EFI_HANDLE ImageHandle,
    EFI_HANDLE DeviceHandle,
    CONST CHAR16 *Path);

static
EFI_STATUS
AbReadManifestFile(
    EFI_FILE_PROTOCOL *Root,
    CONST CHAR16 *Path,
    CHAR8 **OutBuffer,
    UINT32 *OutLength);

static
BOOLEAN
AbIsBmpPayload(CONST UINT8 *Payload, UINTN Length);

static
VOID
AbComputeDestPosition(
    GOP_STATE *State,
    UINT32 FrameWidth,
    UINT32 FrameHeight,
    CONST CHAR8 *Scaling,
    UINT32 *DestX,
    UINT32 *DestY);

EFI_STATUS
EFIAPI
UefiMain(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS Status;
  EFI_FILE_PROTOCOL *Root = NULL;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
  GOP_STATE GopState;
  ANIMATION_CONFIG Config;

  Status = AbInitGopState(&GopState);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  Status = AbOpenRoot(ImageHandle, &Root, &LoadedImage);
  if (EFI_ERROR(Status)) {
    AbRestoreGopState(&GopState);
    return Status;
  }

  // Load animation configuration
  Status = AbLoadAnimationConfig(Root, &Config);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "Failed to load animation config: %r\n", Status));
    // Fall back to default behavior
    Config.AnimationPath = AbDuplicateString(DEFAULT_PACKAGE_PATH);
    Config.ManifestPath = AbDuplicateString(DEFAULT_MANIFEST_PATH);
    Config.UseCustomPartition = FALSE;
  }

  EFI_STATUS PlaybackStatus = AbPlayFromPackage(&Config, &GopState);
  if (EFI_ERROR(PlaybackStatus)) {
    DEBUG((DEBUG_WARN, "Package playback failed: %r\n", PlaybackStatus));

    // If using custom partition failed, try fallback to EFI partition
    if (Config.UseCustomPartition) {
      DEBUG((DEBUG_INFO, "Trying fallback to EFI partition\n"));
      ANIMATION_CONFIG FallbackConfig;
      FallbackConfig.AnimationPath = AbDuplicateString(DEFAULT_PACKAGE_PATH);
      FallbackConfig.ManifestPath = AbDuplicateString(DEFAULT_MANIFEST_PATH);
      FallbackConfig.UseCustomPartition = FALSE;

      PlaybackStatus = AbPlayFromPackage(&FallbackConfig, &GopState);
      AbFreeAnimationConfig(&FallbackConfig);

      if (EFI_ERROR(PlaybackStatus)) {
        PlaybackStatus = AbPlayFromLoose(&Config, &GopState);
        if (EFI_ERROR(PlaybackStatus)) {
          // Try fallback loose manifest too
          FallbackConfig.AnimationPath = AbDuplicateString(DEFAULT_PACKAGE_PATH);
          FallbackConfig.ManifestPath = AbDuplicateString(DEFAULT_MANIFEST_PATH);
          FallbackConfig.UseCustomPartition = FALSE;
          PlaybackStatus = AbPlayFromLoose(&FallbackConfig, &GopState);
          AbFreeAnimationConfig(&FallbackConfig);
        }
      }
    } else {
      PlaybackStatus = AbPlayFromLoose(&Config, &GopState);
    }

    if (EFI_ERROR(PlaybackStatus)) {
      DEBUG((DEBUG_WARN, "Loose manifest playback failed: %r\n", PlaybackStatus));
    }
  }

  AbFreeAnimationConfig(&Config);

  if (Root != NULL) {
    Root->Close(Root);
  }

  AbRestoreGopState(&GopState);

  if (LoadedImage == NULL) {
    return PlaybackStatus;
  }

  EFI_STATUS ChainStatus = AbLaunchNextStage(ImageHandle, LoadedImage->DeviceHandle, DEFAULT_NEXT_STAGE_PATH);
  if (EFI_ERROR(ChainStatus)) {
    DEBUG((DEBUG_ERROR, "Failed to chainload next stage: %r\n", ChainStatus));
    return ChainStatus;
  }
  return EFI_SUCCESS;
}

static EFI_STATUS
AbOpenRoot(
    EFI_HANDLE ImageHandle,
    EFI_FILE_PROTOCOL **Root,
    EFI_LOADED_IMAGE_PROTOCOL **LoadedImage) {
  EFI_STATUS Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;

  if (Root == NULL || LoadedImage == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->HandleProtocol(
      ImageHandle,
      &gEfiLoadedImageProtocolGuid,
      (VOID **)LoadedImage);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol(
      (*LoadedImage)->DeviceHandle,
      &gEfiSimpleFileSystemProtocolGuid,
      (VOID **)&FileSystem);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  return FileSystem->OpenVolume(FileSystem, Root);
}

static EFI_STATUS
AbPlayFromPackage(
    ANIMATION_CONFIG *AnimConfig,
    GOP_STATE *GopState) {
  ANIM_PACKAGE_STATE Package;
  PLAYBACK_CONFIG Config;
  PACKAGE_PLAYBACK_CONTEXT Context;
  EFI_FILE_PROTOCOL *Root = NULL;
  EFI_STATUS Status;

  if (AnimConfig == NULL || AnimConfig->AnimationPath == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Open appropriate filesystem
  if (AnimConfig->UseCustomPartition) {
    Status = AbOpenRootFromPath(AnimConfig->AnimationPath, &Root);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_WARN, "Failed to open custom partition for animation: %r\n", Status));
      return Status;
    }
  } else {
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    Status = AbOpenRoot(gImageHandle, &Root, &LoadedImage);
    if (EFI_ERROR(Status)) {
      return Status;
    }
  }

  // Extract just the file path part for loading
  CHAR16 PartitionSpec[32];
  CHAR16 FilePath[256];
  Status = AbParsePathSpec(AnimConfig->AnimationPath, PartitionSpec,
                          sizeof(PartitionSpec), FilePath, sizeof(FilePath));
  if (EFI_ERROR(Status)) {
    Root->Close(Root);
    return Status;
  }

  ZeroMem(&Package, sizeof(Package));
  Status = AbLoadPackageFromPath(Root, FilePath, &Package);
  if (EFI_ERROR(Status)) {
    Root->Close(Root);
    return Status;
  }

  AbInitPlaybackFromHeader(&Package.Header, &Config);
  if (Package.ManifestJson != NULL && Package.ManifestSize > 0) {
    AbApplyManifestOverrides(Package.ManifestJson, &Config);
  }

  Context.Package = &Package;
  Status = AbRunPlayback(Package.Header.FrameCount, &Config, GopState, AbPackageFrameLoader, &Context);
  AbClosePackage(&Package);
  Root->Close(Root);
  return Status;
}

static EFI_STATUS
AbPlayFromLoose(
    ANIMATION_CONFIG *AnimConfig,
    GOP_STATE *GopState) {
  LOOSE_MANIFEST_STATE Manifest;
  LOOSE_PLAYBACK_CONTEXT Context;
  EFI_FILE_PROTOCOL *Root = NULL;
  EFI_STATUS Status;

  if (AnimConfig == NULL || AnimConfig->ManifestPath == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Open appropriate filesystem
  if (AnimConfig->UseCustomPartition) {
    Status = AbOpenRootFromPath(AnimConfig->ManifestPath, &Root);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_WARN, "Failed to open custom partition for manifest: %r\n", Status));
      return Status;
    }
  } else {
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    Status = AbOpenRoot(gImageHandle, &Root, &LoadedImage);
    if (EFI_ERROR(Status)) {
      return Status;
    }
  }

  // Extract just the file path part for loading
  CHAR16 PartitionSpec[32];
  CHAR16 FilePath[256];
  Status = AbParsePathSpec(AnimConfig->ManifestPath, PartitionSpec,
                          sizeof(PartitionSpec), FilePath, sizeof(FilePath));
  if (EFI_ERROR(Status)) {
    Root->Close(Root);
    return Status;
  }

  ZeroMem(&Manifest, sizeof(Manifest));
  Status = AbLoadLooseManifest(Root, FilePath, &Manifest);
  if (EFI_ERROR(Status)) {
    Root->Close(Root);
    return Status;
  }

  Context.Manifest = &Manifest;
  Context.Root = Root;
  Status = AbRunPlayback(
      Manifest.FrameCount,
      &Manifest.Config,
      GopState,
      AbLooseFrameLoader,
      &Context);
  AbFreeLooseManifest(&Manifest);
  Root->Close(Root);
  return Status;
}

static EFI_STATUS
AbRunPlayback(
    UINT32 FrameCount,
    PLAYBACK_CONFIG *Config,
    GOP_STATE *GopState,
    FRAME_LOADER Loader,
    VOID *Context) {
  EFI_STATUS Status;
  FRAME_BUFFER *Front = NULL;
  FRAME_BUFFER *Back = NULL;
  UINT64 FrameBytes;
  UINT64 TotalBudgetUs;
  UINT64 AccumulatedUs;
  UINT32 LoopIndex;
  UINT32 DestX;
  UINT32 DestY;

  if (FrameCount == 0 || Config == NULL || GopState == NULL || Loader == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Config->LogicalWidth == 0 || Config->LogicalHeight == 0 ||
      Config->LogicalWidth > AB_MAX_FRAME_DIMENSION ||
      Config->LogicalHeight > AB_MAX_FRAME_DIMENSION) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (FrameCount > AB_MAX_FRAME_COUNT) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (Config->LoopCount > AB_MAX_LOOP_COUNT) {
    Config->LoopCount = AB_MAX_LOOP_COUNT;
  }

  if (Config->FrameDurationUs == 0) {
    Config->FrameDurationUs = AB_DEFAULT_FRAME_DURATION;
  }

  FrameBytes = (UINT64)Config->LogicalWidth *
      (UINT64)Config->LogicalHeight *
      sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
  if (FrameBytes == 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if ((FrameBytes * 2) > Config->MaxMemoryBytes) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = AbAllocateFrameBuffer(Config->LogicalWidth, Config->LogicalHeight, &Front);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  Status = AbAllocateFrameBuffer(Config->LogicalWidth, Config->LogicalHeight, &Back);
  if (EFI_ERROR(Status)) {
    AbFreeFrameBuffer(&Front);
    return Status;
  }

  AbComputeDestPosition(
      GopState,
      Config->LogicalWidth,
      Config->LogicalHeight,
      Config->Scaling,
      &DestX,
      &DestY);

  AbFlushKeys();
  AccumulatedUs = 0;
  TotalBudgetUs = (UINT64)Config->MaxTotalDurationMs * 1000ULL;

  for (LoopIndex = 0;
       (Config->LoopCount == 0) || (LoopIndex < Config->LoopCount);
       ++LoopIndex) {
    UINT32 FrameIndex;
    for (FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex) {
      UINT32 DurationUs = Config->FrameDurationUs;
      Status = Loader(Context, FrameIndex, Back, &DurationUs);
      if (EFI_ERROR(Status)) {
        goto Cleanup;
      }

      Status = AbBlitFrame(GopState, Back, DestX, DestY);
      if (EFI_ERROR(Status)) {
        goto Cleanup;
      }

      if (DurationUs < AB_MIN_FRAME_DURATION_US) {
        DurationUs = AB_MIN_FRAME_DURATION_US;
      }

      if (AbUserRequestedSkip(Config->AllowKeySkip)) {
        Status = EFI_ABORTED;
        goto Cleanup;
      }

      AbSwapBuffers(&Front, &Back);
      gBS->Stall(DurationUs);

      AccumulatedUs += DurationUs;
      if (TotalBudgetUs > 0 && AccumulatedUs >= TotalBudgetUs) {
        Status = EFI_SUCCESS;
        goto Cleanup;
      }
    }
  }
  Status = EFI_SUCCESS;

Cleanup:
  AbFreeFrameBuffer(&Front);
  AbFreeFrameBuffer(&Back);
  if (Status == EFI_ABORTED) {
    return EFI_SUCCESS;
  }
  return Status;
}

static EFI_STATUS
AbLoadPackageFromPath(
    EFI_FILE_PROTOCOL *Root,
    CONST CHAR16 *Path,
    ANIM_PACKAGE_STATE *Package) {
  EFI_STATUS Status;
  EFI_FILE_PROTOCOL *File = NULL;
  EFI_FILE_INFO *FileInfo = NULL;
  UINTN Bytes;

  if (Root == NULL || Package == NULL || Path == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Root->Open(Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  ZeroMem(Package, sizeof(*Package));
  Package->Handle = File;

  Bytes = sizeof(ANIM_PACKAGE_HEADER);
  Status = File->Read(File, &Bytes, &Package->Header);
  if (EFI_ERROR(Status) || Bytes != sizeof(ANIM_PACKAGE_HEADER)) {
    Status = EFI_DEVICE_ERROR;
    goto Cleanup;
  }

  if (CompareMem(Package->Header.Magic, ANIM_PACKAGE_MAGIC, 7) != 0) {
    Status = EFI_COMPROMISED_DATA;
    goto Cleanup;
  }

  if (Package->Header.FrameCount == 0 ||
      Package->Header.FrameCount > AB_MAX_FRAME_COUNT) {
    Status = EFI_COMPROMISED_DATA;
    goto Cleanup;
  }

  FileInfo = FileHandleGetInfo(File, &gEfiFileInfoGuid);
  if (FileInfo == NULL) {
    Status = EFI_DEVICE_ERROR;
    goto Cleanup;
  }
  Package->FileSize = FileInfo->FileSize;

  if (Package->Header.ManifestSize > 0) {
    Package->ManifestJson = AllocateZeroPool(Package->Header.ManifestSize + 1);
    if (Package->ManifestJson == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto Cleanup;
    }
    Package->ManifestSize = Package->Header.ManifestSize;
    Bytes = Package->Header.ManifestSize;
    Status = File->Read(File, &Bytes, Package->ManifestJson);
    if (EFI_ERROR(Status) || Bytes != Package->Header.ManifestSize) {
      Status = EFI_DEVICE_ERROR;
      goto Cleanup;
    }
  }

  Bytes = sizeof(ANIM_FRAME_DESC) * Package->Header.FrameCount;
  Package->FrameTable = AllocateZeroPool(Bytes);
  if (Package->FrameTable == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Cleanup;
  }

  Status = File->SetPosition(File, Package->Header.FrameTableOffset);
  if (EFI_ERROR(Status)) {
    goto Cleanup;
  }

  Status = File->Read(File, &Bytes, Package->FrameTable);
  if (EFI_ERROR(Status) || Bytes != sizeof(ANIM_FRAME_DESC) * Package->Header.FrameCount) {
    Status = EFI_DEVICE_ERROR;
    goto Cleanup;
  }

  if (Package->Header.FrameDataOffset >= Package->FileSize) {
    Status = EFI_COMPROMISED_DATA;
    goto Cleanup;
  }

  UINT32 Index;
  for (Index = 0; Index < Package->Header.FrameCount; ++Index) {
    UINT64 Start = (UINT64)Package->Header.FrameDataOffset + Package->FrameTable[Index].Offset;
    UINT64 End = Start + Package->FrameTable[Index].Length;
    if (Start >= Package->FileSize || End > Package->FileSize ||
        Package->FrameTable[Index].Length == 0 ||
        Package->FrameTable[Index].Length > AB_MAX_FRAME_SIZE_BYTES) {
      Status = EFI_COMPROMISED_DATA;
      goto Cleanup;
    }
  }

  Status = EFI_SUCCESS;

Cleanup:
  if (FileInfo != NULL) {
    FreePool(FileInfo);
  }
  if (EFI_ERROR(Status)) {
    AbClosePackage(Package);
  }
  return Status;
}

static VOID
AbClosePackage(ANIM_PACKAGE_STATE *Package) {
  if (Package == NULL) {
    return;
  }
  if (Package->Handle != NULL) {
    Package->Handle->Close(Package->Handle);
  }
  if (Package->ManifestJson != NULL) {
    FreePool(Package->ManifestJson);
  }
  if (Package->FrameTable != NULL) {
    FreePool(Package->FrameTable);
  }
  ZeroMem(Package, sizeof(*Package));
}

static EFI_STATUS
AbLoadLooseManifest(
    EFI_FILE_PROTOCOL *Root,
    CONST CHAR16 *Path,
    LOOSE_MANIFEST_STATE *Manifest) {
  EFI_STATUS Status;
  CHAR8 *Json = NULL;
  UINT32 Length = 0;

  if (Manifest == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  AbInitPlaybackDefaults(&Manifest->Config);

  Status = AbReadManifestFile(Root, Path, &Json, &Length);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  AbApplyManifestOverrides(Json, &Manifest->Config);
  Status = AbParseLooseFrames(Json, Path, Manifest);
  FreePool(Json);
  return Status;
}

static VOID
AbFreeLooseManifest(LOOSE_MANIFEST_STATE *Manifest) {
  UINT32 Index;
  if (Manifest == NULL) {
    return;
  }
  if (Manifest->Frames != NULL) {
    for (Index = 0; Index < Manifest->FrameCount; ++Index) {
      if (Manifest->Frames[Index].Path != NULL) {
        FreePool(Manifest->Frames[Index].Path);
      }
    }
    FreePool(Manifest->Frames);
  }
  ZeroMem(Manifest, sizeof(*Manifest));
}

static EFI_STATUS
AbPackageFrameLoader(
    VOID *Context,
    UINT32 FrameIndex,
    FRAME_BUFFER *Target,
    UINT32 *DurationUs) {
  PACKAGE_PLAYBACK_CONTEXT *PkgContext = (PACKAGE_PLAYBACK_CONTEXT *)Context;
  ANIM_PACKAGE_STATE *Package;
  const ANIM_FRAME_DESC *Descriptor;
  UINT8 *Payload = NULL;
  UINTN PayloadSize;
  EFI_STATUS Status;

  if (PkgContext == NULL || PkgContext->Package == NULL || Target == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Package = PkgContext->Package;
  if (FrameIndex >= Package->Header.FrameCount) {
    return EFI_INVALID_PARAMETER;
  }
  Descriptor = &Package->FrameTable[FrameIndex];
  PayloadSize = Descriptor->Length;

  Payload = AllocatePool(PayloadSize);
  if (Payload == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = AbReadFileChunk(
      Package->Handle,
      (UINT64)Package->Header.FrameDataOffset + Descriptor->Offset,
      Payload,
      PayloadSize);
  if (EFI_ERROR(Status)) {
    goto Cleanup;
  }

  Status = AbDecodeFramePayload(Payload, PayloadSize, Package->Header.PixelFormat, Target);
  if (EFI_ERROR(Status)) {
    goto Cleanup;
  }

  if (DurationUs != NULL && Descriptor->DurationUs != 0) {
    *DurationUs = Descriptor->DurationUs;
  }

Cleanup:
  if (Payload != NULL) {
    FreePool(Payload);
  }
  return Status;
}

static EFI_STATUS
AbLooseFrameLoader(
    VOID *Context,
    UINT32 FrameIndex,
    FRAME_BUFFER *Target,
    UINT32 *DurationUs) {
  LOOSE_PLAYBACK_CONTEXT *LooseContext = (LOOSE_PLAYBACK_CONTEXT *)Context;
  EFI_FILE_PROTOCOL *File = NULL;
  EFI_FILE_INFO *Info = NULL;
  UINT8 *Payload = NULL;
  UINTN PayloadSize;
  EFI_STATUS Status;

  if (LooseContext == NULL ||
      LooseContext->Manifest == NULL ||
      FrameIndex >= LooseContext->Manifest->FrameCount ||
      Target == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  const LOOSE_FRAME_DESC *Frame = &LooseContext->Manifest->Frames[FrameIndex];
  Status = LooseContext->Root->Open(
      LooseContext->Root,
      &File,
      Frame->Path,
      EFI_FILE_MODE_READ,
      0);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  Info = FileHandleGetInfo(File, &gEfiFileInfoGuid);
  if (Info == NULL) {
    Status = EFI_DEVICE_ERROR;
    goto Cleanup;
  }

  if (Info->FileSize == 0 || Info->FileSize > AB_MAX_FRAME_SIZE_BYTES) {
    Status = EFI_COMPROMISED_DATA;
    goto Cleanup;
  }

  PayloadSize = (UINTN)Info->FileSize;
  Payload = AllocatePool(PayloadSize);
  if (Payload == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Cleanup;
  }

  Status = AbReadFileChunk(File, 0, Payload, PayloadSize);
  if (EFI_ERROR(Status)) {
    goto Cleanup;
  }

  ANIM_PIXEL_FORMAT Format = AnimPixelFormatBmp32;
  if (!AbIsBmpPayload(Payload, PayloadSize)) {
    Format = AnimPixelFormatBgra32;
  }

  Status = AbDecodeFramePayload(Payload, PayloadSize, Format, Target);
  if (EFI_ERROR(Status)) {
    goto Cleanup;
  }

  if (DurationUs != NULL && Frame->DurationUs != 0) {
    *DurationUs = Frame->DurationUs;
  }

Cleanup:
  if (Payload != NULL) {
    FreePool(Payload);
  }
  if (Info != NULL) {
    FreePool(Info);
  }
  if (File != NULL) {
    File->Close(File);
  }
  return Status;
}

static EFI_STATUS
AbDecodeFramePayload(
    CONST UINT8 *Payload,
    UINTN PayloadSize,
    ANIM_PIXEL_FORMAT FormatHint,
    FRAME_BUFFER *Target) {
  if (Payload == NULL || Target == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (FormatHint == AnimPixelFormatBgra32) {
    return AbDecodeRawPayload(Payload, PayloadSize, Target);
  }
  return AbDecodeBmpPayload(Payload, PayloadSize, Target);
}

static EFI_STATUS
AbDecodeRawPayload(
    CONST UINT8 *Payload,
    UINTN PayloadSize,
    FRAME_BUFFER *Target) {
  UINTN Expected;

  Expected = Target->Width * Target->Height * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
  if (PayloadSize != Expected) {
    return EFI_COMPROMISED_DATA;
  }
  CopyMem(Target->Pixels, Payload, PayloadSize);
  return EFI_SUCCESS;
}

static EFI_STATUS
AbDecodeBmpPayload(
    CONST UINT8 *Payload,
    UINTN PayloadSize,
    FRAME_BUFFER *Target) {
  const BMP_IMAGE_HEADER *Bmp;
  const BMP_FILE_HEADER *FileHeader;
  const BMP_INFO_HEADER *Info;
  CONST UINT8 *ImageBase;
  UINT32 Row;
  UINT32 Column;
  UINT32 BytesPerPixel;
  UINT32 RowSize;
  BOOLEAN BottomUp;
  INT32 Height;

  if (PayloadSize < sizeof(BMP_IMAGE_HEADER)) {
    return EFI_COMPROMISED_DATA;
  }

  Bmp = (const BMP_IMAGE_HEADER *)Payload;
  FileHeader = &Bmp->BmpHeader.BmpFileHeader;
  Info = &Bmp->BmpHeader.BmpInfoHeader;

  if (FileHeader->CharB != 'B' || FileHeader->CharM != 'M') {
    return EFI_UNSUPPORTED;
  }

  if (Info->BitPerPixel != 24 && Info->BitPerPixel != 32) {
    return EFI_UNSUPPORTED;
  }

  Height = Info->PixelHeight;
  if ((UINT32)Info->PixelWidth != Target->Width ||
      ((Height < 0 ? -Height : Height) != (INT32)Target->Height)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (Info->CompressionType != 0) {
    return EFI_UNSUPPORTED;
  }

  if (FileHeader->ImageOffset >= PayloadSize) {
    return EFI_COMPROMISED_DATA;
  }

  BytesPerPixel = Info->BitPerPixel / 8;
  RowSize = ((Info->BitPerPixel * Target->Width + 31) / 32) * 4;
  if ((UINT64)RowSize * Target->Height > PayloadSize) {
    return EFI_COMPROMISED_DATA;
  }

  ImageBase = Payload + FileHeader->ImageOffset;
  BottomUp = Info->PixelHeight > 0;

  for (Row = 0; Row < Target->Height; ++Row) {
    UINT32 SrcRow = BottomUp ? (Target->Height - 1 - Row) : Row;
    CONST UINT8 *Src = ImageBase + (UINTN)SrcRow * RowSize;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Dst = Target->Pixels + (UINTN)Row * Target->PitchPixels;
    for (Column = 0; Column < Target->Width; ++Column) {
      CONST UINT8 *Pixel = Src + Column * BytesPerPixel;
      Dst[Column].Blue = Pixel[0];
      Dst[Column].Green = Pixel[1];
      Dst[Column].Red = Pixel[2];
      Dst[Column].Reserved = (BytesPerPixel == 4) ? Pixel[3] : 0xFF;
    }
  }
  return EFI_SUCCESS;
}

static EFI_STATUS
AbReadFileChunk(
    EFI_FILE_PROTOCOL *File,
    UINT64 Offset,
    VOID *Buffer,
    UINTN Length) {
  EFI_STATUS Status;
  UINTN Bytes = Length;

  Status = File->SetPosition(File, Offset);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  Status = File->Read(File, &Bytes, Buffer);
  if (EFI_ERROR(Status) || Bytes != Length) {
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}

static VOID
AbInitPlaybackDefaults(PLAYBACK_CONFIG *Config) {
  if (Config == NULL) {
    return;
  }
  Config->LogicalWidth = 640;
  Config->LogicalHeight = 360;
  Config->FrameDurationUs = AB_DEFAULT_FRAME_DURATION;
  Config->LoopCount = 1;
  Config->AllowKeySkip = TRUE;
  Config->MaxMemoryBytes = AB_DEFAULT_MAX_MEMORY_BYTES;
  Config->MaxTotalDurationMs = 0;
  AsciiStrCpyS(Config->Scaling, sizeof(Config->Scaling), "letterbox");
}

static VOID
AbInitPlaybackFromHeader(
    const ANIM_PACKAGE_HEADER *Header,
    PLAYBACK_CONFIG *Config) {
  AbInitPlaybackDefaults(Config);
  if (Header == NULL || Config == NULL) {
    return;
  }
  Config->LogicalWidth = Header->LogicalWidth;
  Config->LogicalHeight = Header->LogicalHeight;
  Config->LoopCount = Header->LoopCount;
  if (Header->TargetFps != 0) {
    Config->FrameDurationUs = (UINT32)(1000000U / Header->TargetFps);
  }
  if (Config->FrameDurationUs == 0) {
    Config->FrameDurationUs = AB_DEFAULT_FRAME_DURATION;
  }
}

static VOID
AbApplyManifestOverrides(
    CHAR8 *Json,
    PLAYBACK_CONFIG *Config) {
  UINT64 Value;
  CHAR8 Buffer[sizeof(Config->Scaling)];
  BOOLEAN BoolVal;

  if (Json == NULL || Config == NULL) {
    return;
  }

  if (AbJsonReadUint(Json, "logical_width", &Value) && Value > 0) {
    Config->LogicalWidth = (UINT32)MIN(Value, AB_MAX_FRAME_DIMENSION);
  }
  if (AbJsonReadUint(Json, "logical_height", &Value) && Value > 0) {
    Config->LogicalHeight = (UINT32)MIN(Value, AB_MAX_FRAME_DIMENSION);
  }
  if (AbJsonReadUint(Json, "frame_duration_us", &Value) && Value > 0) {
    Config->FrameDurationUs = (UINT32)Value;
  }
  if (AbJsonReadUint(Json, "loop_count", &Value)) {
    Config->LoopCount = (UINT32)MIN(Value, AB_MAX_LOOP_COUNT);
  }
  if (AbJsonReadUint(Json, "max_memory", &Value) && Value > 0) {
    Config->MaxMemoryBytes = Value;
  }
  if (AbJsonReadUint(Json, "max_total_duration_ms", &Value)) {
    Config->MaxTotalDurationMs = (UINT32)Value;
  }
  if (AbJsonReadBool(Json, "allow_key_skip", &BoolVal)) {
    Config->AllowKeySkip = BoolVal;
  }
  if (AbJsonReadString(Json, "scaling", Buffer, sizeof(Buffer))) {
    AsciiStrCpyS(Config->Scaling, sizeof(Config->Scaling), Buffer);
  }
}

static CHAR8 *
AbJsonFindKey(
    CHAR8 *Json,
    CONST CHAR8 *Key) {
  CHAR8 Pattern[64];

  if (Json == NULL || Key == NULL) {
    return NULL;
  }
  AsciiSPrint(Pattern, sizeof(Pattern), "\"%a\"", Key);
  return AsciiStrStr(Json, Pattern);
}

static CHAR8 *
AbSkipJsonNoise(CHAR8 *Ptr) {
  if (Ptr == NULL) {
    return NULL;
  }
  while (*Ptr != '\0' &&
         (*Ptr == ' ' || *Ptr == '\t' || *Ptr == '\n' ||
          *Ptr == '\r' || *Ptr == ':' || *Ptr == ',')) {
    ++Ptr;
  }
  return Ptr;
}

static BOOLEAN
AbJsonReadUint(
    CHAR8 *Json,
    CONST CHAR8 *Key,
    UINT64 *Value) {
  CHAR8 *KeyPtr;
  CHAR8 *ValuePtr;
  RETURN_STATUS ConvStatus;
  UINTN KeyLength;

  if (Value == NULL) {
    return FALSE;
  }
  KeyPtr = AbJsonFindKey(Json, Key);
  if (KeyPtr == NULL) {
    return FALSE;
  }
  KeyLength = AsciiStrLen(Key);
  ValuePtr = AbSkipJsonNoise(KeyPtr + KeyLength + 2);
  if (ValuePtr == NULL) {
    return FALSE;
  }
  ConvStatus = AsciiStrDecimalToUint64S(ValuePtr, NULL, Value);
  return !RETURN_ERROR(ConvStatus);
}

static BOOLEAN
AbJsonReadBool(
    CHAR8 *Json,
    CONST CHAR8 *Key,
    BOOLEAN *Value) {
  CHAR8 *KeyPtr;
  CHAR8 *ValuePtr;
  UINTN KeyLength;

  if (Value == NULL) {
    return FALSE;
  }
  KeyPtr = AbJsonFindKey(Json, Key);
  if (KeyPtr == NULL) {
    return FALSE;
  }
  KeyLength = AsciiStrLen(Key);
  ValuePtr = AbSkipJsonNoise(KeyPtr + KeyLength + 2);
  if (ValuePtr == NULL) {
    return FALSE;
  }
  if (AsciiStrnCmp(ValuePtr, "true", 4) == 0) {
    *Value = TRUE;
    return TRUE;
  }
  if (AsciiStrnCmp(ValuePtr, "false", 5) == 0) {
    *Value = FALSE;
    return TRUE;
  }
  return FALSE;
}

static BOOLEAN
AbJsonReadString(
    CHAR8 *Json,
    CONST CHAR8 *Key,
    CHAR8 *Buffer,
    UINTN BufferSize) {
  CHAR8 *KeyPtr;
  CHAR8 *ValuePtr;
  UINTN Index;
  UINTN KeyLength;

  if (Buffer == NULL || BufferSize == 0) {
    return FALSE;
  }

  KeyPtr = AbJsonFindKey(Json, Key);
  if (KeyPtr == NULL) {
    return FALSE;
  }
  KeyLength = AsciiStrLen(Key);
  ValuePtr = AbSkipJsonNoise(KeyPtr + KeyLength + 2);
  if (ValuePtr == NULL || *ValuePtr != '"') {
    return FALSE;
  }
  ++ValuePtr;
  for (Index = 0; Index + 1 < BufferSize && ValuePtr[Index] != '\0'; ++Index) {
    if (ValuePtr[Index] == '"') {
      break;
    }
    Buffer[Index] = ValuePtr[Index];
  }
  Buffer[Index] = '\0';
  return TRUE;
}

static EFI_STATUS
AbParseLooseFrames(
    CHAR8 *Json,
    CONST CHAR16 *ManifestPath,
    LOOSE_MANIFEST_STATE *Manifest) {
  CHAR8 *FramesKey;
  CHAR8 *ArrayStart;
  CHAR8 *ArrayEnd;
  CHAR8 *Cursor;
  UINT32 ObjectCount = 0;
  UINT32 Index = 0;
  CHAR16 *BaseDirectory = NULL;
  EFI_STATUS Status = EFI_SUCCESS;

  if (Json == NULL || Manifest == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  FramesKey = AbJsonFindKey(Json, "frames");
  if (FramesKey == NULL) {
    return EFI_NOT_FOUND;
  }

  ArrayStart = AsciiStrStr(FramesKey, "[");
  ArrayEnd = AsciiStrStr(FramesKey, "]");
  if (ArrayStart == NULL || ArrayEnd == NULL || ArrayEnd <= ArrayStart) {
    return EFI_COMPROMISED_DATA;
  }

  for (Cursor = ArrayStart; Cursor < ArrayEnd; ++Cursor) {
    if (*Cursor == '{') {
      ++ObjectCount;
    }
  }

  if (ObjectCount == 0) {
    return EFI_NOT_FOUND;
  }

  Manifest->Frames = AllocateZeroPool(sizeof(LOOSE_FRAME_DESC) * ObjectCount);
  if (Manifest->Frames == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Manifest->FrameCount = ObjectCount;

  BaseDirectory = AbExtractDirectory(ManifestPath);
  if (BaseDirectory == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Cleanup;
  }

  Cursor = ArrayStart;
  while (Cursor < ArrayEnd && Index < ObjectCount) {
    CHAR8 *ObjectStart = AsciiStrStr(Cursor, "{");
    CHAR8 *ObjectEnd;
    CHAR8 *Segment;
    UINTN SegmentLength;
    if (ObjectStart == NULL || ObjectStart >= ArrayEnd) {
      break;
    }
    ObjectEnd = AsciiStrStr(ObjectStart, "}");
    if (ObjectEnd == NULL || ObjectEnd >= ArrayEnd) {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    SegmentLength = (UINTN)(ObjectEnd - ObjectStart + 1);
    Segment = AllocateZeroPool(SegmentLength + 1);
    if (Segment == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      break;
    }
    CopyMem(Segment, ObjectStart, SegmentLength);
    Status = AbParseFrameObject(Segment, BaseDirectory, &Manifest->Frames[Index]);
    FreePool(Segment);
    if (EFI_ERROR(Status)) {
      break;
    }
    Index++;
    Cursor = ObjectEnd + 1;
  }

  if (!EFI_ERROR(Status) && Index != ObjectCount) {
    Status = EFI_COMPROMISED_DATA;
  }

Cleanup:
  if (EFI_ERROR(Status)) {
    AbFreeLooseManifest(Manifest);
  }
  if (BaseDirectory != NULL) {
    FreePool(BaseDirectory);
  }
  return Status;
}

static EFI_STATUS
AbParseFrameObject(
    CHAR8 *ObjectJson,
    CONST CHAR16 *BaseDirectory,
    LOOSE_FRAME_DESC *Frame) {
  CHAR8 PathBuffer[256];
  UINT64 Duration = 0;
  CHAR16 *Relative = NULL;
  EFI_STATUS Status = EFI_SUCCESS;

  if (Frame == NULL || ObjectJson == NULL || BaseDirectory == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!AbJsonReadString(ObjectJson, "path", PathBuffer, sizeof(PathBuffer))) {
    return EFI_NOT_FOUND;
  }

  Relative = AbAsciiPathToUnicode(PathBuffer);
  if (Relative == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Frame->Path = AbJoinPaths(BaseDirectory, Relative);
  FreePool(Relative);
  if (Frame->Path == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (AbJsonReadUint(ObjectJson, "duration_us", &Duration)) {
    Frame->DurationUs = (UINT32)Duration;
  }
  return Status;
}

static CHAR16 *
AbDuplicateString(CONST CHAR16 *Source) {
  UINTN Size;
  CHAR16 *Copy;
  if (Source == NULL) {
    return NULL;
  }
  Size = StrSize(Source);
  Copy = AllocateZeroPool(Size);
  if (Copy != NULL) {
    CopyMem(Copy, Source, Size);
  }
  return Copy;
}

static CHAR16 *
AbExtractDirectory(CONST CHAR16 *Path) {
  UINTN Length;
  CHAR16 *Result;

  if (Path == NULL) {
    return NULL;
  }

  Length = StrLen(Path);
  while (Length > 0) {
    if (Path[Length - 1] == L'\\' || Path[Length - 1] == L'/') {
      break;
    }
    --Length;
  }

  if (Length == 0) {
    Result = AllocateZeroPool(3 * sizeof(CHAR16));
    if (Result != NULL) {
      Result[0] = L'\\';
      Result[1] = L'\0';
    }
    return Result;
  }

  Result = AllocateZeroPool((Length + 2) * sizeof(CHAR16));
  if (Result == NULL) {
    return NULL;
  }
  CopyMem(Result, Path, Length * sizeof(CHAR16));
  Result[Length] = L'\0';
  AbNormalizePathSeparators(Result);
  if (Result[Length - 1] != L'\\') {
    Result[Length] = L'\\';
    Result[Length + 1] = L'\0';
  }
  return Result;
}

static VOID
AbNormalizePathSeparators(CHAR16 *Path) {
  UINTN Index;
  if (Path == NULL) {
    return;
  }
  for (Index = 0; Path[Index] != L'\0'; ++Index) {
    if (Path[Index] == L'/') {
      Path[Index] = L'\\';
    }
  }
}

static CHAR16 *
AbJoinPaths(CONST CHAR16 *Base, CONST CHAR16 *Child) {
  UINTN BaseLen;
  UINTN ChildLen;
  CHAR16 *Result;

  if (Child == NULL) {
    return NULL;
  }

  if (Child[0] == L'\\') {
    return AbDuplicateString(Child);
  }

  BaseLen = (Base != NULL) ? StrLen(Base) : 0;
  ChildLen = StrLen(Child);

  Result = AllocateZeroPool((BaseLen + 1 + ChildLen + 1) * sizeof(CHAR16));
  if (Result == NULL) {
    return NULL;
  }
  if (BaseLen > 0) {
    CopyMem(Result, Base, BaseLen * sizeof(CHAR16));
  }
  if (BaseLen == 0 || Result[BaseLen - 1] != L'\\') {
    Result[BaseLen] = L'\\';
    BaseLen += 1;
  }
  CopyMem(Result + BaseLen, Child, (ChildLen + 1) * sizeof(CHAR16));
  AbNormalizePathSeparators(Result);
  return Result;
}

static CHAR16 *
AbAsciiPathToUnicode(CONST CHAR8 *Path) {
  UINTN Length;
  CHAR16 *Result;
  UINTN Index;

  if (Path == NULL) {
    return NULL;
  }
  Length = AsciiStrLen(Path);
  Result = AllocateZeroPool((Length + 1) * sizeof(CHAR16));
  if (Result == NULL) {
    return NULL;
  }
  for (Index = 0; Index < Length; ++Index) {
    CHAR8 Ch = Path[Index];
    if (Ch == '/') {
      Result[Index] = L'\\';
    } else {
      Result[Index] = (CHAR16)Ch;
    }
  }
  Result[Length] = L'\0';
  return Result;
}

static BOOLEAN
AbUserRequestedSkip(BOOLEAN AllowSkip) {
  EFI_STATUS Status;
  EFI_KEY_DATA KeyData;
  EFI_INPUT_KEY Key;

  if (!AllowSkip) {
    return FALSE;
  }

  AbInitInputProtocols();
  if (mTextInputEx != NULL) {
    Status = mTextInputEx->ReadKeyStrokeEx(mTextInputEx, &KeyData);
    if (!EFI_ERROR(Status)) {
      return TRUE;
    }
  }

  if (gST != NULL && gST->ConIn != NULL) {
    Status = gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
    if (!EFI_ERROR(Status)) {
      return TRUE;
    }
  }
  return FALSE;
}

static VOID
AbInitInputProtocols(VOID) {
  if (mTextInputEx != NULL) {
    return;
  }
  gBS->LocateProtocol(
      &gEfiSimpleTextInputExProtocolGuid,
      NULL,
      (VOID **)&mTextInputEx);
}

static VOID
AbFlushKeys(VOID) {
  EFI_KEY_DATA KeyData;
  EFI_INPUT_KEY Key;

  AbInitInputProtocols();
  if (mTextInputEx != NULL) {
    while (!EFI_ERROR(mTextInputEx->ReadKeyStrokeEx(mTextInputEx, &KeyData))) {
    }
  }
  if (gST != NULL && gST->ConIn != NULL) {
    while (!EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &Key))) {
    }
  }
}

static EFI_STATUS
AbLaunchNextStage(
    EFI_HANDLE ImageHandle,
    EFI_HANDLE DeviceHandle,
    CONST CHAR16 *Path) {
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;
  EFI_HANDLE NextImage = NULL;
  EFI_STATUS Status;

  DevicePath = FileDevicePath(DeviceHandle, Path);
  if (DevicePath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->LoadImage(
      FALSE,
      ImageHandle,
      DevicePath,
      NULL,
      0,
      &NextImage);
  if (!EFI_ERROR(Status)) {
    Status = gBS->StartImage(NextImage, NULL, NULL);
  }
  FreePool(DevicePath);
  return Status;
}

static
EFI_STATUS
AbReadManifestFile(
    EFI_FILE_PROTOCOL *Root,
    CONST CHAR16 *Path,
    CHAR8 **OutBuffer,
    UINT32 *OutLength) {
  EFI_FILE_PROTOCOL *File = NULL;
  EFI_FILE_INFO *Info = NULL;
  EFI_STATUS Status;

  if (OutBuffer == NULL || OutLength == NULL || Root == NULL || Path == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Root->Open(Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  Info = FileHandleGetInfo(File, &gEfiFileInfoGuid);
  if (Info == NULL || Info->FileSize == 0) {
    Status = EFI_DEVICE_ERROR;
    goto Cleanup;
  }

  if (Info->FileSize > 256 * 1024) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Cleanup;
  }

  *OutBuffer = AllocateZeroPool((UINTN)Info->FileSize + 1);
  if (*OutBuffer == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Cleanup;
  }

  *OutLength = (UINT32)Info->FileSize;
  Status = AbReadFileChunk(File, 0, *OutBuffer, *OutLength);

Cleanup:
  if (EFI_ERROR(Status) && *OutBuffer != NULL) {
    FreePool(*OutBuffer);
    *OutBuffer = NULL;
  }
  if (EFI_ERROR(Status)) {
    *OutLength = 0;
  }
  if (Info != NULL) {
    FreePool(Info);
  }
  if (File != NULL) {
    File->Close(File);
  }
  return Status;
}

static
BOOLEAN
AbIsBmpPayload(CONST UINT8 *Payload, UINTN Length) {
  if (Payload == NULL || Length < 2) {
    return FALSE;
  }
  return (Payload[0] == 'B' && Payload[1] == 'M');
}

static
VOID
AbComputeDestPosition(
    GOP_STATE *State,
    UINT32 FrameWidth,
    UINT32 FrameHeight,
    CONST CHAR8 *Scaling,
    UINT32 *DestX,
    UINT32 *DestY) {
  UINT32 ScreenWidth;
  UINT32 ScreenHeight;

  if (State == NULL || State->Gop == NULL || DestX == NULL || DestY == NULL) {
    return;
  }

  ScreenWidth = State->Gop->Mode->Information->HorizontalResolution;
  ScreenHeight = State->Gop->Mode->Information->VerticalResolution;

  if (ScreenWidth <= FrameWidth) {
    *DestX = 0;
  } else {
    *DestX = (ScreenWidth - FrameWidth) / 2;
  }

  if (ScreenHeight <= FrameHeight) {
    *DestY = 0;
  } else {
    *DestY = (ScreenHeight - FrameHeight) / 2;
  }

  if (Scaling != NULL && AsciiStrCmp(Scaling, "fill") == 0) {
    *DestX = 0;
    *DestY = 0;
  }
}

static EFI_STATUS
AbOpenRootFromPath(
    CONST CHAR16 *PathSpec,
    EFI_FILE_PROTOCOL **Root) {
  EFI_STATUS Status;
  CHAR16 PartitionSpec[32];
  CHAR16 FilePath[256];
  EFI_HANDLE PartitionHandle = NULL;

  if (Root == NULL || PathSpec == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Root = NULL;

  // Parse path specification
  Status = AbParsePathSpec(PathSpec, PartitionSpec, sizeof(PartitionSpec),
                          FilePath, sizeof(FilePath));
  if (EFI_ERROR(Status)) {
    return Status;
  }

  // If no partition spec, use default EFI partition
  if (PartitionSpec[0] == L'\0') {
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_HANDLE ImageHandle;

    // Get current image handle
    Status = gBS->HandleProtocol(
        gImageHandle,
        &gEfiLoadedImageProtocolGuid,
        (VOID **)&LoadedImage);
    if (EFI_ERROR(Status)) {
      return Status;
    }

    return AbOpenRoot(gImageHandle, Root, &LoadedImage);
  }

  // Find partition by label or path
  Status = AbFindPartitionByLabel(PartitionSpec, &PartitionHandle);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "Failed to find partition '%s': %r\n", PartitionSpec, Status));
    return Status;
  }

  // Open file system on the partition
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;
  Status = gBS->HandleProtocol(
      PartitionHandle,
      &gEfiSimpleFileSystemProtocolGuid,
      (VOID **)&FileSystem);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  return FileSystem->OpenVolume(FileSystem, Root);
}

static EFI_STATUS
AbEnumerateFatPartitions(
    EFI_HANDLE **Handles,
    UINTN *HandleCount) {
  EFI_STATUS Status;
  UINTN BufferSize = 0;
  EFI_HANDLE *HandleBuffer = NULL;

  if (Handles == NULL || HandleCount == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Handles = NULL;
  *HandleCount = 0;

  // Get all handles with SimpleFileSystem protocol
  Status = gBS->LocateHandleBuffer(
      ByProtocol,
      &gEfiSimpleFileSystemProtocolGuid,
      NULL,
      &BufferSize,
      &HandleBuffer);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  // Filter FAT partitions (we'll check this by trying to open them)
  EFI_HANDLE *FatHandles = NULL;
  UINTN FatCount = 0;

  FatHandles = AllocateZeroPool(BufferSize);
  if (FatHandles == NULL) {
    FreePool(HandleBuffer);
    return EFI_OUT_OF_RESOURCES;
  }

  for (UINTN i = 0; i < BufferSize / sizeof(EFI_HANDLE); i++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;
    EFI_FILE_PROTOCOL *Root = NULL;

    Status = gBS->HandleProtocol(
        HandleBuffer[i],
        &gEfiSimpleFileSystemProtocolGuid,
        (VOID **)&FileSystem);
    if (EFI_ERROR(Status)) {
      continue;
    }

    Status = FileSystem->OpenVolume(FileSystem, &Root);
    if (EFI_ERROR(Status)) {
      continue;
    }

    // Try to read a small file to verify it's accessible
    EFI_FILE_PROTOCOL *TestFile = NULL;
    Status = Root->Open(Root, &TestFile, L"\\", EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(Status)) {
      TestFile->Close(TestFile);
      FatHandles[FatCount++] = HandleBuffer[i];
    }

    Root->Close(Root);
  }

  FreePool(HandleBuffer);

  if (FatCount == 0) {
    FreePool(FatHandles);
    return EFI_NOT_FOUND;
  }

  *Handles = FatHandles;
  *HandleCount = FatCount;
  return EFI_SUCCESS;
}

static EFI_STATUS
AbFindPartitionByLabel(
    CONST CHAR16 *Label,
    EFI_HANDLE *Handle) {
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  UINTN HandleCount = 0;
  CHAR16 PartitionLabel[64];

  if (Handle == NULL || Label == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Handle = NULL;

  Status = AbEnumerateFatPartitions(&Handles, &HandleCount);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  for (UINTN i = 0; i < HandleCount; i++) {
    Status = AbGetPartitionLabel(Handles[i], PartitionLabel, sizeof(PartitionLabel));
    if (!EFI_ERROR(Status)) {
      if (StrCmp(Label, PartitionLabel) == 0) {
        *Handle = Handles[i];
        FreePool(Handles);
        return EFI_SUCCESS;
      }
    }
  }

  FreePool(Handles);
  return EFI_NOT_FOUND;
}

static EFI_STATUS
AbGetPartitionLabel(
    EFI_HANDLE Handle,
    CHAR16 *Label,
    UINTN LabelSize) {
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath = NULL;
  EFI_DEVICE_PATH_PROTOCOL *Node = NULL;

  if (Label == NULL || LabelSize == 0) {
    return EFI_INVALID_PARAMETER;
  }

  Label[0] = L'\0';

  // Try to get partition label from device path
  Status = gBS->HandleProtocol(
      Handle,
      &gEfiDevicePathProtocolGuid,
      (VOID **)&DevicePath);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  Node = DevicePath;
  while (!IsDevicePathEnd(Node)) {
    if (DevicePathType(Node) == MEDIA_DEVICE_PATH &&
        DevicePathSubType(Node) == MEDIA_PARTITION_DP) {
      HARDDRIVE_DEVICE_PATH *HdNode = (HARDDRIVE_DEVICE_PATH *)Node;
      // For now, return a generic label. In a full implementation,
      // you might want to read the partition table or filesystem
      UnicodeSPrint(Label, LabelSize, L"PART%02X", HdNode->PartitionNumber);
      return EFI_SUCCESS;
    }
    Node = NextDevicePathNode(Node);
  }

  // Fallback: try to get from BlockIo
  Status = gBS->HandleProtocol(
      Handle,
      &gEfiBlockIoProtocolGuid,
      (VOID **)&BlockIo);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  // Use media ID as label
  UnicodeSPrint(Label, LabelSize, L"BLK%08X", BlockIo->Media->MediaId);
  return EFI_SUCCESS;
}

static EFI_STATUS
AbParsePathSpec(
    CONST CHAR16 *PathSpec,
    CHAR16 *PartitionSpec,
    UINTN PartitionSpecSize,
    CHAR16 *FilePath,
    UINTN FilePathSize) {
  CONST CHAR16 *ColonPos;
  UINTN PartitionLen;
  UINTN PathLen;

  if (PathSpec == NULL || PartitionSpec == NULL || FilePath == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Look for colon separator (format: PARTITION:\path\to\file)
  ColonPos = StrStr(PathSpec, L":");
  if (ColonPos == NULL) {
    // No partition spec, use default
    PartitionSpec[0] = L'\0';
    StrnCpyS(FilePath, FilePathSize / sizeof(CHAR16), PathSpec,
             FilePathSize / sizeof(CHAR16) - 1);
    return EFI_SUCCESS;
  }

  // Extract partition spec
  PartitionLen = (UINTN)(ColonPos - PathSpec);
  if (PartitionLen >= PartitionSpecSize / sizeof(CHAR16)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  StrnCpyS(PartitionSpec, PartitionSpecSize / sizeof(CHAR16), PathSpec, PartitionLen);
  PartitionSpec[PartitionLen] = L'\0';

  // Extract file path
  PathLen = StrLen(ColonPos + 1);
  if (PathLen >= FilePathSize / sizeof(CHAR16)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  StrCpyS(FilePath, FilePathSize / sizeof(CHAR16), ColonPos + 1);
  return EFI_SUCCESS;
}

static EFI_STATUS
AbLoadAnimationConfig(
    EFI_FILE_PROTOCOL *Root,
    ANIMATION_CONFIG *Config) {
  EFI_STATUS Status;
  CHAR8 *Json = NULL;
  UINT32 Length = 0;
  CHAR8 PathBuffer[256];

  if (Config == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem(Config, sizeof(ANIMATION_CONFIG));

  // Try to load config from default location
  Status = AbReadManifestFile(Root, L"\\EFI\\AnimeBoot\\config.json", &Json, &Length);
  if (EFI_ERROR(Status)) {
    // No config file, use defaults
    Config->AnimationPath = AbDuplicateString(DEFAULT_PACKAGE_PATH);
    Config->ManifestPath = AbDuplicateString(DEFAULT_MANIFEST_PATH);
    Config->UseCustomPartition = FALSE;
    return EFI_SUCCESS;
  }

  // Parse config JSON
  if (AbJsonReadString(Json, "animation_path", PathBuffer, sizeof(PathBuffer))) {
    Config->AnimationPath = AbAsciiPathToUnicode(PathBuffer);
    Config->UseCustomPartition = (StrStr(Config->AnimationPath, L":") != NULL);
  } else {
    Config->AnimationPath = AbDuplicateString(DEFAULT_PACKAGE_PATH);
    Config->UseCustomPartition = FALSE;
  }

  if (AbJsonReadString(Json, "manifest_path", PathBuffer, sizeof(PathBuffer))) {
    Config->ManifestPath = AbAsciiPathToUnicode(PathBuffer);
    if (!Config->UseCustomPartition) {
      Config->UseCustomPartition = (StrStr(Config->ManifestPath, L":") != NULL);
    }
  } else {
    Config->ManifestPath = AbDuplicateString(DEFAULT_MANIFEST_PATH);
  }

  FreePool(Json);
  return EFI_SUCCESS;
}

static VOID
AbFreeAnimationConfig(ANIMATION_CONFIG *Config) {
  if (Config == NULL) {
    return;
  }

  if (Config->AnimationPath != NULL) {
    FreePool(Config->AnimationPath);
    Config->AnimationPath = NULL;
  }

  if (Config->ManifestPath != NULL) {
    FreePool(Config->ManifestPath);
    Config->ManifestPath = NULL;
  }

  Config->UseCustomPartition = FALSE;
}


