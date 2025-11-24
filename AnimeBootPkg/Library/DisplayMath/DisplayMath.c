#include "DisplayMath.h"

EFI_STATUS
AbCalcLetterboxRect(
    UINT32 ScreenWidth,
    UINT32 ScreenHeight,
    UINT32 FrameWidth,
    UINT32 FrameHeight,
    FRAME_RECT *Rect) {
  UINT64 TargetWidth;
  UINT64 TargetHeight;
  UINT64 ScreenRatioTimes1000;
  UINT64 FrameRatioTimes1000;

  if (Rect == NULL || ScreenWidth == 0 || ScreenHeight == 0 ||
      FrameWidth == 0 || FrameHeight == 0) {
    return EFI_INVALID_PARAMETER;
  }

  ScreenRatioTimes1000 = ((UINT64)ScreenWidth * 1000) / ScreenHeight;
  FrameRatioTimes1000 = ((UINT64)FrameWidth * 1000) / FrameHeight;

  if (FrameRatioTimes1000 > ScreenRatioTimes1000) {
    TargetWidth = ScreenWidth;
    TargetHeight = (UINT64)FrameHeight * ScreenWidth / FrameWidth;
  } else {
    TargetHeight = ScreenHeight;
    TargetWidth = (UINT64)FrameWidth * ScreenHeight / FrameHeight;
  }

  Rect->Width = (UINT32)TargetWidth;
  Rect->Height = (UINT32)TargetHeight;
  Rect->X = (ScreenWidth - Rect->Width) / 2;
  Rect->Y = (ScreenHeight - Rect->Height) / 2;
  return EFI_SUCCESS;
}

