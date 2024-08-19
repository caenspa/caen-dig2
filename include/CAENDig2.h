/******************************************************************************
*
*	CAEN SpA - Software Division
*	Via Vetraia, 11 - 55049 - Viareggio ITALY
*	+39 0594 388 398 - www.caen.it
*
*******************************************************************************
*
*	Copyright (C) 2020-2023 CAEN SpA
*
*	This file is part of the CAEN Dig2 Library.
*
*	The CAEN Dig2 Library is free software; you can redistribute it and/or
*	modify it under the terms of the GNU Lesser General Public
*	License as published by the Free Software Foundation; either
*	version 3 of the License, or (at your option) any later version.
*
*	The CAEN Dig2 Library is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
*	Lesser General Public License for more details.
*
*	You should have received a copy of the GNU Lesser General Public
*	License along with the CAEN Dig2 Library; if not, see
*	https://www.gnu.org/licenses/.
*
*	SPDX-License-Identifier: LGPL-3.0-or-later
*
***************************************************************************//*!
*
*	\file		CAENDig2.h
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CAENDIG2_H_
#define CAEN_INCLUDE_CAENDIG2_H_

#include <CAEN_FELib.h>

#define CAEN_DIG2_VERSION_MAJOR		1
#define CAEN_DIG2_VERSION_MINOR		6
#define CAEN_DIG2_VERSION_PATCH		1
#define CAEN_DIG2_VERSION			(CAEN_DIG2_VERSION_MAJOR * 10000) + (CAEN_DIG2_VERSION_MINOR * 100) + (CAEN_DIG2_VERSION_PATCH)
#define CAEN_DIG2_VERSION_STRING	CAEN_FELIB_STR(CAEN_DIG2_VERSION_MAJOR) "." CAEN_FELIB_STR(CAEN_DIG2_VERSION_MINOR) "." CAEN_FELIB_STR(CAEN_DIG2_VERSION_PATCH)

#ifdef __cplusplus
extern "C" {
#endif

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_GetLibInfo(char* jsonString, size_t size);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_GetLibVersion(char version[16]);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_GetLastError(char description[1024]);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_DevicesDiscovery(char* jsonString, size_t size, int timeout);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_Open(const char* url, uint32_t* handle);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_Close(uint32_t handle);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_GetDeviceTree(uint32_t handle, char* jsonString, size_t size);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_GetChildHandles(uint32_t handle, const char* path, uint32_t* handles, size_t size);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_GetHandle(uint32_t handle, const char* path, uint32_t* pathHandle);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_GetParentHandle(uint32_t handle, const char* path, uint32_t* parentHandle);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_GetPath(uint32_t handle, char path[256]);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_GetNodeProperties(uint32_t handle, const char* path, char name[32], CAEN_FELib_NodeType_t* type);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_GetValue(uint32_t handle, const char* path, char value[256]);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_SetValue(uint32_t handle, const char* path, const char* value);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_SendCommand(uint32_t handle, const char* path);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_GetUserRegister(uint32_t handle, uint32_t address, uint32_t* value);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_SetUserRegister(uint32_t handle, uint32_t address, uint32_t value);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_SetReadDataFormat(uint32_t handle, const char* jsonString);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_ReadDataV(uint32_t handle, int timeout, va_list args);

CAEN_FELIB_DLLAPI int CAEN_FELIB_API CAENDig2_HasData(uint32_t handle, int timeout);

#ifdef __cplusplus
}
#endif

#endif /* CAEN_INCLUDE_CAENDIG2_H_ */
