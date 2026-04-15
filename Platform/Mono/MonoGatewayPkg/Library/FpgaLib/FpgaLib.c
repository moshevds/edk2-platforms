/** @file
  Mono board clock/personality hooks.
**/

#include <Library/DebugLib.h>
#include <Library/FpgaLib.h>

UINTN
GetBoardSysClk (
  VOID
  )
{
  return 100000000;
}

VOID
PrintBoardPersonality (
  VOID
  )
{
  DEBUG ((DEBUG_INFO, "MONO board personality\n"));
}
