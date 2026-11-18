#pragma once
#include <guiddef.h>

// CLSID of the modern-menu command provider (IExplorerCommand).
// Keep in sync with native/packaging/AppxManifest.xml.
// {B7F3A2C1-9D4E-4F6A-8B2C-5E1D3A7F9B04}
inline const GUID CLSID_LegacyMenuCommand =
{ 0xb7f3a2c1, 0x9d4e, 0x4f6a, { 0x8b, 0x2c, 0x5e, 0x1d, 0x3a, 0x7f, 0x9b, 0x04 } };

// Stable canonical name for the root command.
// {3E6B1C2A-4D5F-4A7B-9C8D-1E2F3A4B5C6D}
inline const GUID GUID_RootCommand =
{ 0x3e6b1c2a, 0x4d5f, 0x4a7b, { 0x9c, 0x8d, 0x1e, 0x2f, 0x3a, 0x4b, 0x5c, 0x6d } };
