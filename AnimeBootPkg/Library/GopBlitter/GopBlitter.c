#include "GopBlitter.h"

EFI_STATUS
AbInitGopState(GOP_STATE *State) {
  EFI_STATUS Status;
  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = gBS->LocateProtocol(
      &gEfiGraphicsOutputProtocolGuid,
      NULL,
      (VOID **)&State->Gop);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  State->OriginalMode = State->Gop->Mode->Information;
  State->OriginalModeIndex = State->Gop->Mode->Mode;
  return EFI_SUCCESS;
}

VOID
AbRestoreGopState(GOP_STATE *State) {
  if (State == NULL || State->Gop == NULL) {
    return;
  }
  State->Gop->SetMode(State->Gop, State->OriginalModeIndex);
}

EFI_STATUS
AbAllocateFrameBuffer(
    UINT32 Width,
    UINT32 Height,
    FRAME_BUFFER **Buffer) {
  EFI_STATUS Status;
  FRAME_BUFFER *Frame;
  if (Buffer == NULL || Width == 0 || Height == 0) {
    return EFI_INVALID_PARAMETER;
  }
  Frame = AllocateZeroPool(sizeof(FRAME_BUFFER));
  if (Frame == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Frame->Width = Width;
  Frame->Height = Height;
  Frame->PitchPixels = Width;
  Frame->Pixels = AllocateZeroPool(
      Width * Height * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  if (Frame->Pixels == NULL) {
    FreePool(Frame);
    return EFI_OUT_OF_RESOURCES;
  }
  *Buffer = Frame;
  return EFI_SUCCESS;
}

VOID
AbFreeFrameBuffer(FRAME_BUFFER **Buffer) {
  FRAME_BUFFER *Frame;
  if (Buffer == NULL || *Buffer == NULL) {
    return;
  }
  Frame = *Buffer;
  if (Frame->Pixels != NULL) {
    FreePool(Frame->Pixels);
  }
  FreePool(Frame);
  *Buffer = NULL;
}

EFI_STATUS
AbBlitFrame(
    GOP_STATE *State,
    FRAME_BUFFER *Frame,
    UINT32 DestX,
    UINT32 DestY) {
  if (State == NULL || State->Gop == NULL || Frame == NULL || Frame->Pixels == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  return State->Gop->Blt(
      State->Gop,
      Frame->Pixels,
      EfiBltBufferToVideo,
      0,
      0,
      DestX,
      DestY,
      Frame->Width,
      Frame->Height,
      Frame->PitchPixels * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
}

VOID
AbSwapBuffers(FRAME_BUFFER **Front, FRAME_BUFFER **Back) {
  FRAME_BUFFER *Temp;
  if (Front == NULL || Back == NULL) {
    return;
  }
  Temp = *Front;
  *Front = *Back;
  *Back = Temp;
}

