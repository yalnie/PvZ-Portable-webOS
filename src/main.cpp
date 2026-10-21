/*
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PvZ-Portable.
 *
 * PvZ-Portable is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PvZ-Portable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with PvZ-Portable. If not, see <https://www.gnu.org/licenses/>.
 */

#include "LawnApp.h"
#include "Resources.h"
#include "PvzpLib/PvzpStringFile.h"
#include <cstdlib>
#include <vector>
using namespace Sexy;

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#ifdef __SWITCH__
#include <switch.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _WIN32
static std::vector<std::string> gUtf8ArgsStorage;
static std::vector<char*> gUtf8Argv;

static void BuildUtf8ArgsFromWin32(int& argc, char**& argv)
{
	int aWideArgc = 0;
	LPWSTR* aWideArgv = CommandLineToArgvW(GetCommandLineW(), &aWideArgc);
	if (aWideArgv == nullptr || aWideArgc <= 0)
		return;

	gUtf8ArgsStorage.clear();
	gUtf8Argv.clear();
	gUtf8ArgsStorage.reserve(static_cast<size_t>(aWideArgc));
	gUtf8Argv.reserve(static_cast<size_t>(aWideArgc));

	for (int i = 0; i < aWideArgc; ++i)
	{
		const wchar_t* aWide = aWideArgv[i];
		int aLen = WideCharToMultiByte(CP_UTF8, 0, aWide, -1, nullptr, 0, nullptr, nullptr);
		if (aLen <= 0)
		{
			gUtf8ArgsStorage.emplace_back();
		}
		else
		{
			std::string aUtf8;
			aUtf8.resize(static_cast<size_t>(aLen - 1));
			WideCharToMultiByte(CP_UTF8, 0, aWide, -1, aUtf8.data(), aLen, nullptr, nullptr);
			gUtf8ArgsStorage.emplace_back(std::move(aUtf8));
		}
	}

	for (auto& aStr : gUtf8ArgsStorage)
		gUtf8Argv.push_back(const_cast<char*>(aStr.c_str()));

	argc = static_cast<int>(gUtf8Argv.size());
	argv = gUtf8Argv.data();

	LocalFree(aWideArgv);
}
#endif

#if defined(WEBOS) || defined(__WEBOS__)
#include <SDL.h>
#include <signal.h>
#include <unistd.h>

static void WebOSSignalHandler(int sig)
{
	const char* msg = "Unknown signal";
	if (sig == SIGSEGV) msg = "SIGSEGV (Segmentation fault)";
	else if (sig == SIGABRT) msg = "SIGABRT (Aborted)";
	else if (sig == SIGBUS) msg = "SIGBUS (Bus error)";
	else if (sig == SIGFPE) msg = "SIGFPE (Floating point exception)";
	else if (sig == SIGILL) msg = "SIGILL (Illegal instruction)";

	fprintf(stderr, "[PvZ FATAL] Crash caught by signal handler: %s (%d)\n", msg, sig);
	FILE* f = fopen("/tmp/pvz_crash.log", "w");
	if (f)
	{
		fprintf(f, "Crash signal: %s (%d)\n", msg, sig);
		fclose(f);
	}
	_exit(128 + sig);
}
#endif

int main(int argc, char** argv)
{
#if defined(WEBOS) || defined(__WEBOS__)
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);

	signal(SIGSEGV, WebOSSignalHandler);
	signal(SIGABRT, WebOSSignalHandler);
	signal(SIGBUS, WebOSSignalHandler);
	signal(SIGFPE, WebOSSignalHandler);
	signal(SIGILL, WebOSSignalHandler);

	// Auto-detect Wayland environment for SSH sessions
	if (!getenv("XDG_RUNTIME_DIR"))
	{
		const char* candidates[] = {
			"/tmp/xdg",
			"/run/user/0",
			"/tmp/run/0",
			"/var/run/xdg",
			"/run/user/5000",
			"/tmp/run/user/0",
			nullptr
		};
		for (int i = 0; candidates[i]; ++i)
		{
			std::string socketPath = std::string(candidates[i]) + "/wayland-0";
			if (access(socketPath.c_str(), F_OK) == 0)
			{
				setenv("XDG_RUNTIME_DIR", candidates[i], 1);
				fprintf(stderr, "[PvZ] Auto-detected XDG_RUNTIME_DIR: %s\n", candidates[i]);
				break;
			}
		}
	}
	if (!getenv("WAYLAND_DISPLAY"))
	{
		setenv("WAYLAND_DISPLAY", "wayland-0", 0);
	}

	// App ID hint for Wayland / webOS LSM
	SDL_SetHint("SDL_APP_ID", "io.github.wszqkzqk.pvzportable");
	SDL_SetHint("SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK", "true");
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

	// Initialize SDL video/audio early so PmLogLib and SDL_LogMessage don't crash before window creation
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
	{
		fprintf(stderr, "[PvZ] SDL_Init failed: %s\n", SDL_GetError());
	}
#endif

#ifdef __SWITCH__
	consoleDebugInit(debugDevice_SVC);
#endif

#ifdef _WIN32
	BuildUtf8ArgsFromWin32(argc, argv);
#endif

	PvzpStringListSetColors(gLawnStringFormats, gLawnStringFormatCount);
	gExtractResourcesByName = Sexy::ExtractResourcesByName;
	gLawnApp = new LawnApp();
	gLawnApp->SetArgs(argc, argv);
	gLawnApp->Init();
	gLawnApp->Start();
#ifndef __EMSCRIPTEN__
	gLawnApp->Shutdown();
	delete gLawnApp;
#endif

	return 0;
};
