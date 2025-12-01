#ifndef ANIMEBOOT_MAIN_H_
#define ANIMEBOOT_MAIN_H_

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FileHandleLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextInEx.h>

#include "AnimFormat.h"

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *OriginalMode;
  UINT32 OriginalModeIndex;
} GOP_STATE;

typedef struct {
  UINT32 Width;
  UINT32 Height;
  UINT32 PitchPixels;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Pixels;
} FRAME_BUFFER;

typedef struct {
  EFI_FILE_PROTOCOL *Root;
  CHAR16 *BasePath;
} FILE_CONTEXT;

typedef struct {
  CHAR16 *AnimationPath;     // Path to animation file (can include partition spec)
  CHAR16 *ManifestPath;      // Path to manifest file (can include partition spec)
  BOOLEAN UseCustomPartition; // Whether to use custom partition instead of EFI partition
} ANIMATION_CONFIG;

#endif  // ANIMEBOOT_MAIN_H_

