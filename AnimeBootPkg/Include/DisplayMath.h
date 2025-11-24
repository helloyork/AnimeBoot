#ifndef ANIMEBOOT_DISPLAY_MATH_H_
#define ANIMEBOOT_DISPLAY_MATH_H_

#include <Uefi.h>

typedef struct {
  UINT32 X;
  UINT32 Y;
  UINT32 Width;
  UINT32 Height;
} FRAME_RECT;

EFI_STATUS
AbCalcLetterboxRect(
  UINT32 ScreenWidth,
  UINT32 ScreenHeight,
  UINT32 FrameWidth,
  UINT32 FrameHeight,
  FRAME_RECT *Rect
  );

#endif  // ANIMEBOOT_DISPLAY_MATH_H_

