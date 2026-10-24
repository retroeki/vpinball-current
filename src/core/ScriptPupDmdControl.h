// license:GPLv3+
//
// PUPDMDControl.DMD shim for the standalone build.
//
// PinUP Player tables that drive a real (hardware) DMD create the PuP DMD
// driver when the table is configured for one:
//
//   if PuPDMDDriverType=pDMDTypeReal then
//      Set PUPDMDObject = CreateObject("PUPDMDControl.DMD")
//      PUPDMDObject.DMDOpen
//      PUPDMDObject.DMDPuPMirror
//      PUPDMDObject.DMDPuPTextMirror
//
// There is no PuP DMD driver on Android, so CreateObject raised
// VBSE_CANT_CREATE_OBJECT (429) and the table died at init (Superman TAS,
// Batman TAS, Eminem, Spongebob: 10 reports in the 2026-10-23 crash batch).
//
// This shim returns a no-op IDispatch for the driver's methods; each returns
// Empty. Any other name fails with 438, like the other host stubs.
//
// Wired in via def.cpp::external_create_object alongside ScriptShellApplication.

#pragma once

#include <oaidl.h>

// Returns a freshly-AddRef'd IDispatch implementing PUPDMDControl.DMD.
// Caller owns the reference. Returns nullptr on allocation failure.
IDispatch* CreateScriptPupDmdControl();
