#ifndef ANIMEBOOT_ANIM_FORMAT_H_
#define ANIMEBOOT_ANIM_FORMAT_H_

#include <Uefi.h>

#pragma pack(push, 1)

typedef struct {
  CHAR8   Magic[8];         // "ABANIM\0"
  UINT16  VersionMajor;
  UINT16  VersionMinor;
  UINT16  HeaderSize;
  UINT16  Flags;
  UINT32  ManifestSize;
  UINT32  FrameCount;
  UINT32  FrameTableOffset;
  UINT32  FrameDataOffset;
  UINT32  LogicalWidth;
  UINT32  LogicalHeight;
  UINT32  PixelFormat;      // 0 = BGRA32 raw, 1 = BMP 32bpp
  UINT32  TargetFps;
  UINT32  LoopCount;
  UINT32  Reserved[6];
} ANIM_PACKAGE_HEADER;

typedef struct {
  UINT64 Offset;
  UINT32 Length;
  UINT32 DurationUs;
} ANIM_FRAME_DESC;

#pragma pack(pop)

#define ANIM_PACKAGE_MAGIC       "ABANIM\0"
#define ANIM_PACKAGE_VERSION_MAJ 1
#define ANIM_PACKAGE_VERSION_MIN 0

typedef enum {
  AnimPixelFormatBgra32 = 0,
  AnimPixelFormatBmp32  = 1
} ANIM_PIXEL_FORMAT;

typedef struct {
  UINT32 LogicalWidth;
  UINT32 LogicalHeight;
  UINT32 FrameDurationUs;
  UINT32 LoopCount;
  BOOLEAN AllowKeySkip;
  UINT64 MaxMemoryBytes;
} ANIM_MANIFEST_LIMITS;

#endif  // ANIMEBOOT_ANIM_FORMAT_H_

