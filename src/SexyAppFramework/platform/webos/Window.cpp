/*
 * Portions of this file are based on the PopCap Games Framework
 * Copyright (C) 2005-2009 PopCap Games, Inc.
 *
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later AND LicenseRef-PopCap
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

#include <SDL.h>

#include <memory>
#include <type_traits>
#include <unistd.h>
#include <climits>

#include "SexyAppBase.h"
#include "graphics/GLInterface.h"
#include "graphics/GLImage.h"
#include "graphics/GLPlatform.h"
#include "widget/WidgetManager.h"

#ifndef SDL_HINT_APP_ID
#define SDL_HINT_APP_ID "SDL_APP_ID"
#endif

#ifndef SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK
#define SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK "SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK"
#endif

namespace Sexy
{

std::string GetWebOSAppDir()
{
	char buf[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n <= 0)
		return ".";
	buf[n] = '\0';
	std::string exePath(buf);
	size_t slash = exePath.find_last_of('/');
	if (slash == std::string::npos)
		return ".";
	return exePath.substr(0, slash);
}

void SexyAppBase::MakeWindow()
{
	if (mWindow)
	{
		return;
	}

	// App ID hint for Wayland / webOS LSM
	SDL_SetHint(SDL_HINT_APP_ID, "io.github.wszqkzqk.pvzportable");

	// Intercept the TV remote's Back key so LSM forwards it as key events
	SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK, "true");

	// Disable synthetic mouse generation from touch, and vice versa
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

	int winW = 1920;
	int winH = 1080;

	// webOS shell requires fullscreen passed directly at creation time
	Uint32 winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN;

	using WindowPtr = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
	using GLContextPtr = std::unique_ptr<std::remove_pointer_t<SDL_GLContext>, decltype(&SDL_GL_DeleteContext)>;

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	WindowPtr window(SDL_CreateWindow(
		mTitle.c_str(),
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		winW, winH, winFlags), &SDL_DestroyWindow);

	GLContextPtr context(nullptr, &SDL_GL_DeleteContext);

	if (window)
		context.reset(SDL_GL_CreateContext(window.get()));

	if (!context)
	{
		Sexy::LogErrorLn("Failed to create OpenGL ES 2.0 context on webOS: {}", SDL_GetError());
		return;
	}

	SDL_GL_SetSwapInterval(1);

	mWindow = (void*)window.release();
	mContext = (void*)context.release();

	if (mGLInterface == nullptr)
	{
		mGLInterface = std::make_unique<GLInterface>(this);
		if (!InitGLInterface())
		{
			mGLInterface = nullptr;
			return;
		}
	}

	mActive = true;
	mMinimized = false;
	mPhysMinimized = false;

	ReInitImages();

	mWidgetManager->mImage = mGLInterface->GetScreenImage();
	mWidgetManager->MarkAllDirty();

	mGLInterface->UpdateViewport();
	mWidgetManager->Resize(mScreenBounds, mGLInterface->mPresentationRect);
}

} // namespace Sexy

std::string GetWebOSAppDir()
{
	return Sexy::GetWebOSAppDir();
}

