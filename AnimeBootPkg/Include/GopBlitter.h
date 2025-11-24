#ifndef ANIMEBOOT_GOP_BLITTER_H_
#define ANIMEBOOT_GOP_BLITTER_H_

#include "AnimeBoot.h"

EFI_STATUS
AbInitGopState(GOP_STATE *State);

VOID
AbRestoreGopState(GOP_STATE *State);

EFI_STATUS
AbAllocateFrameBuffer(
  UINT32 Width,
  UINT32 Height,
  FRAME_BUFFER **Buffer
  );

VOID
AbFreeFrameBuffer(
  FRAME_BUFFER **Buffer
  );

EFI_STATUS
AbBlitFrame(
  GOP_STATE *State,
  FRAME_BUFFER *Frame,
  UINT32 DestX,
  UINT32 DestY
  );

VOID
AbSwapBuffers(
  FRAME_BUFFER **Front,
  FRAME_BUFFER **Back
  );

#endif  // ANIMEBOOT_GOP_BLITTER_H_

