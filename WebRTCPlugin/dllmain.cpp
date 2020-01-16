// dllmain.cpp : Implementation of DllMain.

#include "stdafx.h"
#include "resource.h"
#include "WebRTCPlugin_i.h"
#include "dllmain.h"

CWebRTCPluginModule _AtlModule;

HINSTANCE g_hInstance = nullptr;

// DLL Entry Point
extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	g_hInstance = hInstance;
	return _AtlModule.DllMain(dwReason, lpReserved);
}
