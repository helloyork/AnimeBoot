[Defines]
  PLATFORM_NAME                  = AnimeBoot
  PLATFORM_GUID                  = 6F074C1F-5A8A-4AF2-8CF0-4DE812A74FEE
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x0001001A
  SUPPORTED_ARCHITECTURES        = IA32|X64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT

[LibraryClasses]
  BaseLib                           | MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib                     | MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  MemoryAllocationLib               | MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  UefiBootServicesTableLib          | MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiLib                           | MdePkg/Library/UefiLib/UefiLib.inf
  DebugLib                          | MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf
  PrintLib                          | MdePkg/Library/BasePrintLib/BasePrintLib.inf
  PcdLib                            | MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  DevicePathLib                     | MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  FileHandleLib                     | MdePkg/Library/UefiFileHandleLib/UefiFileHandleLib.inf
  GopBlitterLib                     | AnimeBootPkg/Library/GopBlitter/GopBlitter.inf
  DisplayMathLib                    | AnimeBootPkg/Library/DisplayMath/DisplayMath.inf

[Components]
  AnimeBootPkg/Application/AnimeBoot/AnimeBoot.inf

