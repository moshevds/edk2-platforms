/** @file
  Mono Gateway embedded device tree manager protocol.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef MONO_DT_MANAGER_PROTOCOL_H_
#define MONO_DT_MANAGER_PROTOCOL_H_

#include <Uefi.h>

#define MONO_DT_NAME_MAX_CHARS  64

typedef struct {
  EFI_GUID  FileGuid;
  CHAR16    Name[MONO_DT_NAME_MAX_CHARS];
} MONO_DT_BLOB_DESCRIPTOR;

typedef struct _MONO_DT_MANAGER_PROTOCOL MONO_DT_MANAGER_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *MONO_DT_MANAGER_GET_EMBEDDED_DTBS)(
  IN  MONO_DT_MANAGER_PROTOCOL          *This,
  OUT UINTN                             *Count,
  OUT CONST MONO_DT_BLOB_DESCRIPTOR     **Dtbs
  );

typedef
EFI_STATUS
(EFIAPI *MONO_DT_MANAGER_GET_ACTIVE_DTB)(
  IN  MONO_DT_MANAGER_PROTOCOL  *This,
  OUT INTN                      *ActiveIndex
  );

typedef
EFI_STATUS
(EFIAPI *MONO_DT_MANAGER_SELECT_DTB)(
  IN MONO_DT_MANAGER_PROTOCOL  *This,
  IN UINTN                     Index
  );

typedef
EFI_STATUS
(EFIAPI *MONO_DT_MANAGER_CLEAR_DTB)(
  IN MONO_DT_MANAGER_PROTOCOL  *This
  );

struct _MONO_DT_MANAGER_PROTOCOL {
  MONO_DT_MANAGER_GET_EMBEDDED_DTBS    GetEmbeddedDtbs;
  MONO_DT_MANAGER_GET_ACTIVE_DTB       GetActiveDtb;
  MONO_DT_MANAGER_SELECT_DTB           SelectDtb;
  MONO_DT_MANAGER_CLEAR_DTB            ClearDtb;
};

extern EFI_GUID  gMonoDtManagerProtocolGuid;

#endif
