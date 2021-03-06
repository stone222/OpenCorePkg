/** @file
  OpenCore driver.

Copyright (c) 2019, vit9696. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <OpenCore.h>

#include <Guid/OcVariables.h>
#include <Guid/GlobalVariable.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/OcAppleBootCompatLib.h>
#include <Library/OcAppleBootPolicyLib.h>
#include <Library/OcAppleEventLib.h>
#include <Library/OcAppleImageConversionLib.h>
#include <Library/OcAudioLib.h>
#include <Library/OcInputLib.h>
#include <Library/OcAppleKeyMapLib.h>
#include <Library/OcAppleUserInterfaceThemeLib.h>
#include <Library/OcConsoleLib.h>
#include <Library/OcCpuLib.h>
#include <Library/OcDataHubLib.h>
#include <Library/OcDevicePropertyLib.h>
#include <Library/OcDriverConnectionLib.h>
#include <Library/OcFirmwareVolumeLib.h>
#include <Library/OcHashServicesLib.h>
#include <Library/OcMiscLib.h>
#include <Library/OcSmcLib.h>
#include <Library/OcOSInfoLib.h>
#include <Library/OcUnicodeCollationEngLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

BOOLEAN
OcLoadUefiInputSupport (
  IN OC_GLOBAL_CONFIG  *Config
  )
{
  BOOLEAN               ExitBs;
  EFI_STATUS            Status;
  UINT32                TimerResolution;
  CONST CHAR8           *PointerSupportStr;
  OC_INPUT_POINTER_MODE PointerMode;
  OC_INPUT_KEY_MODE     KeyMode;
  CONST CHAR8           *KeySupportStr;

  ExitBs = FALSE;

  TimerResolution = Config->Uefi.Input.TimerResolution;
  if (TimerResolution != 0) {
    Status = OcAppleGenericInputTimerQuirkInit (TimerResolution);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "OC: Failed to initialize timer quirk\n"));
    } else {
      ExitBs = TRUE;
    }
  }

  if (Config->Uefi.Input.PointerSupport) {
    PointerSupportStr = OC_BLOB_GET (&Config->Uefi.Input.PointerSupportMode);
    PointerMode = OcInputPointerModeMax;
    if (AsciiStrCmp (PointerSupportStr, "ASUS") == 0) {
      PointerMode = OcInputPointerModeAsus;
    } else {
      DEBUG ((DEBUG_WARN, "OC: Invalid input pointer mode %a\n", PointerSupportStr));
    }

    if (PointerMode != OcInputPointerModeMax) {
      Status = OcAppleGenericInputPointerInit (PointerMode);
      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "OC: Failed to initialize pointer\n"));
      } else {
        ExitBs = TRUE;
      }
    }
  }

  if (Config->Uefi.Input.KeySupport) {
    KeySupportStr = OC_BLOB_GET (&Config->Uefi.Input.KeySupportMode);
    KeyMode = OcInputKeyModeMax;
    if (AsciiStrCmp (KeySupportStr, "Auto") == 0) {
      KeyMode = OcInputKeyModeAuto;
    } else if (AsciiStrCmp (KeySupportStr, "V1") == 0) {
      KeyMode = OcInputKeyModeV1;
    } else if (AsciiStrCmp (KeySupportStr, "V2") == 0) {
      KeyMode = OcInputKeyModeV2;
    } else if (AsciiStrCmp (KeySupportStr, "AMI") == 0) {
      KeyMode = OcInputKeyModeAmi;
    } else {
      DEBUG ((DEBUG_WARN, "OC: Invalid input key mode %a\n", KeySupportStr));
    }

    if (KeyMode != OcInputKeyModeMax) {
      Status = OcAppleGenericInputKeycodeInit (
                 KeyMode,
                 Config->Uefi.Input.KeyForgetThreshold,
                 Config->Uefi.Input.KeyMergeThreshold,
                 Config->Uefi.Input.KeySwap
                 );
      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "OC: Failed to initialize keycode\n"));
      } else {
        ExitBs = TRUE;
      }
    }
  }

  return ExitBs;
}

VOID
OcLoadUefiOutputSupport (
  IN OC_GLOBAL_CONFIG  *Config
  )
{
  EFI_STATUS           Status;
  CONST CHAR8          *AsciiRenderer;
  OC_CONSOLE_RENDERER  Renderer;
  UINT32               Width;
  UINT32               Height;
  UINT32               Bpp;
  BOOLEAN              SetMax;

  if (Config->Uefi.Output.ProvideConsoleGop) {
    OcProvideConsoleGop (TRUE);
  }

  OcParseScreenResolution (
    OC_BLOB_GET (&Config->Uefi.Output.Resolution),
    &Width,
    &Height,
    &Bpp,
    &SetMax
    );

  DEBUG ((
    DEBUG_INFO,
    "OC: Requested resolution is %ux%u@%u (max: %d) from %a\n",
    Width,
    Height,
    Bpp,
    SetMax,
    OC_BLOB_GET (&Config->Uefi.Output.Resolution)
    ));

  if (SetMax || (Width > 0 && Height > 0)) {
    Status = OcSetConsoleResolution (
      Width,
      Height,
      Bpp
      );
    DEBUG ((
      EFI_ERROR (Status) ? DEBUG_WARN : DEBUG_INFO,
      "OC: Changed resolution to %ux%u@%u (max: %d) from %a - %r\n",
      Width,
      Height,
      Bpp,
      SetMax,
      OC_BLOB_GET (&Config->Uefi.Output.Resolution),
      Status
      ));
  }

  if (Config->Uefi.Output.DirectGopRendering) {
    OcUseDirectGop ();
  }

  if (Config->Uefi.Output.ReconnectOnResChange) {
    OcReconnectConsole ();
  }

  AsciiRenderer = OC_BLOB_GET (&Config->Uefi.Output.TextRenderer);

  if (AsciiRenderer[0] == '\0' || AsciiStrCmp (AsciiRenderer, "BuiltinGraphics") == 0) {
    Renderer = OcConsoleRendererBuiltinGraphics;
  } else if (AsciiStrCmp (AsciiRenderer, "SystemGraphics") == 0) {
    Renderer = OcConsoleRendererSystemGraphics;
  } else if (AsciiStrCmp (AsciiRenderer, "SystemText") == 0) {
    Renderer = OcConsoleRendererSystemText;
  } else if (AsciiStrCmp (AsciiRenderer, "SystemGeneric") == 0) {
    Renderer = OcConsoleRendererSystemGeneric;
  } else {
    DEBUG ((DEBUG_WARN, "OC: Requested unknown renderer %a\n", AsciiRenderer));
    Renderer = OcConsoleRendererBuiltinGraphics;
  }

  OcSetupConsole (
    Renderer,
    Config->Uefi.Output.IgnoreTextInGraphics,
    Config->Uefi.Output.SanitiseClearScreen,
    Config->Uefi.Output.ClearScreenOnModeSwitch,
    Config->Uefi.Output.ReplaceTabWithSpace
    );

  OcParseConsoleMode (
    OC_BLOB_GET (&Config->Uefi.Output.ConsoleMode),
    &Width,
    &Height,
    &SetMax
    );

  DEBUG ((
    DEBUG_INFO,
    "OC: Requested console mode is %ux%u (max: %d) from %a\n",
    Width,
    Height,
    SetMax,
    OC_BLOB_GET (&Config->Uefi.Output.ConsoleMode)
    ));

  if (SetMax || (Width > 0 && Height > 0)) {
    Status = OcSetConsoleMode (Width, Height);
    DEBUG ((
      EFI_ERROR (Status) ? DEBUG_WARN : DEBUG_INFO,
      "OC: Changed console mode to %ux%u (max: %d) from %a - %r\n",
      Width,
      Height,
      SetMax,
      OC_BLOB_GET (&Config->Uefi.Output.ConsoleMode),
      Status
      ));
  }
}
