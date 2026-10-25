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

#include <algorithm>

#include "SexyAppBase.h"
#include "graphics/GLInterface.h"
#include "graphics/GLImage.h"
#include "widget/WidgetManager.h"
#include "misc/KeyCodes.h"

// Scancode delivered by LG webOS for the Magic Remote Back button
#ifndef WEBOS_SCANCODE_BACK
#define WEBOS_SCANCODE_BACK 482
#endif

using namespace Sexy;

// Map SDL_Keycode to internal KeyCode (Windows VK-compatible).
static KeyCode SDLKeyToKeyCode(SDL_Keycode theSDLKey)
{
	if (theSDLKey >= SDLK_a && theSDLKey <= SDLK_z)
		return static_cast<KeyCode>(theSDLKey - SDLK_a + 'A');

	if (theSDLKey >= SDLK_0 && theSDLKey <= SDLK_9)
		return static_cast<KeyCode>(theSDLKey);

	switch (theSDLKey)
	{
		case SDLK_BACKSPACE:    return KEYCODE_BACK;
		case SDLK_TAB:          return KEYCODE_TAB;
		case SDLK_CLEAR:        return KEYCODE_CLEAR;
		case SDLK_RETURN:       return KEYCODE_RETURN;
		case SDLK_AC_BACK:
		case SDLK_ESCAPE:       return KEYCODE_ESCAPE;
		case SDLK_SPACE:        return KEYCODE_SPACE;
		case SDLK_DELETE:       return KEYCODE_DELETE;

		case SDLK_LEFT:         return KEYCODE_LEFT;
		case SDLK_UP:           return KEYCODE_UP;
		case SDLK_RIGHT:        return KEYCODE_RIGHT;
		case SDLK_DOWN:         return KEYCODE_DOWN;

		case SDLK_INSERT:       return KEYCODE_INSERT;
		case SDLK_HOME:         return KEYCODE_HOME;
		case SDLK_END:          return KEYCODE_END;
		case SDLK_PAGEUP:       return KEYCODE_PRIOR;
		case SDLK_PAGEDOWN:     return KEYCODE_NEXT;

		case SDLK_LSHIFT:
		case SDLK_RSHIFT:       return KEYCODE_SHIFT;
		case SDLK_LCTRL:
		case SDLK_RCTRL:        return KEYCODE_CONTROL;
		case SDLK_LALT:
		case SDLK_RALT:         return KEYCODE_MENU;
		case SDLK_PAUSE:        return KEYCODE_PAUSE;
		case SDLK_CAPSLOCK:     return KEYCODE_CAPITAL;
		case SDLK_NUMLOCKCLEAR: return KEYCODE_NUMLOCK;
		case SDLK_SCROLLLOCK:   return KEYCODE_SCROLL;

		case SDLK_KP_0:         return KEYCODE_NUMPAD0;
		case SDLK_KP_1:         return KEYCODE_NUMPAD1;
		case SDLK_KP_2:         return KEYCODE_NUMPAD2;
		case SDLK_KP_3:         return KEYCODE_NUMPAD3;
		case SDLK_KP_4:         return KEYCODE_NUMPAD4;
		case SDLK_KP_5:         return KEYCODE_NUMPAD5;
		case SDLK_KP_6:         return KEYCODE_NUMPAD6;
		case SDLK_KP_7:         return KEYCODE_NUMPAD7;
		case SDLK_KP_8:         return KEYCODE_NUMPAD8;
		case SDLK_KP_9:         return KEYCODE_NUMPAD9;
		case SDLK_KP_MULTIPLY:  return KEYCODE_MULTIPLY;
		case SDLK_KP_PLUS:      return KEYCODE_ADD;
		case SDLK_KP_MINUS:     return KEYCODE_SUBTRACT;
		case SDLK_KP_PERIOD:    return KEYCODE_DECIMAL;
		case SDLK_KP_DIVIDE:    return KEYCODE_DIVIDE;
		case SDLK_KP_ENTER:     return KEYCODE_RETURN;

		case SDLK_F1:           return KEYCODE_F1;
		case SDLK_F2:           return KEYCODE_F2;
		case SDLK_F3:           return KEYCODE_F3;
		case SDLK_F4:           return KEYCODE_F4;
		case SDLK_F5:           return KEYCODE_F5;
		case SDLK_F6:           return KEYCODE_F6;
		case SDLK_F7:           return KEYCODE_F7;
		case SDLK_F8:           return KEYCODE_F8;
		case SDLK_F9:           return KEYCODE_F9;
		case SDLK_F10:          return KEYCODE_F10;
		case SDLK_F11:          return KEYCODE_F11;
		case SDLK_F12:          return KEYCODE_F12;

		default:                return KEYCODE_UNKNOWN;
	}
}

static bool SDLSynthesizeAsciiCharFromKeyDown(const SDL_KeyboardEvent& theEvent, char& theChar)
{
	theChar = 0;

	SDL_Keycode aSym = theEvent.keysym.sym;
	SDL_Keymod aMods = static_cast<SDL_Keymod>(theEvent.keysym.mod);
	const bool aHasCtrl = (aMods & KMOD_CTRL) != 0;
	const bool aHasAlt = (aMods & KMOD_ALT) != 0;
	const bool aHasGui = (aMods & KMOD_GUI) != 0;
	const bool aHasShift = (aMods & KMOD_SHIFT) != 0;
	const bool aTextInputActive = SDL_IsTextInputActive();

	if (aHasAlt || aHasGui)
		return false;

	if (aSym >= SDLK_a && aSym <= SDLK_z)
	{
		if (aHasCtrl)
		{
			theChar = static_cast<char>(aSym - SDLK_a + 1);
			return true;
		}

		if (aTextInputActive)
			return false;

		theChar = static_cast<char>(aHasShift ? aSym - SDLK_a + 'A' : aSym);
		return true;
	}

	if (aHasCtrl || aTextInputActive)
		return false;

	switch (aSym)
	{
		case SDLK_KP_1: theChar = '1'; return true;
		case SDLK_KP_2: theChar = '2'; return true;
		case SDLK_KP_3: theChar = '3'; return true;
		case SDLK_KP_4: theChar = '4'; return true;
		case SDLK_KP_5: theChar = '5'; return true;
		case SDLK_KP_6: theChar = '6'; return true;
		case SDLK_KP_7: theChar = '7'; return true;
		case SDLK_KP_8: theChar = '8'; return true;
		case SDLK_KP_9: theChar = '9'; return true;
		case SDLK_KP_0: theChar = '0'; return true;
		case SDLK_KP_PLUS: theChar = '+'; return true;
		case SDLK_KP_MINUS: theChar = '-'; return true;
		case SDLK_KP_MULTIPLY: theChar = '*'; return true;
		case SDLK_KP_DIVIDE: theChar = '/'; return true;
		case SDLK_KP_PERIOD: theChar = '.'; return true;
		case SDLK_KP_EQUALS: theChar = '='; return true;
		case SDLK_1: theChar = aHasShift ? '!' : '1'; return true;
		case SDLK_2: theChar = aHasShift ? '@' : '2'; return true;
		case SDLK_3: theChar = aHasShift ? '#' : '3'; return true;
		case SDLK_4: theChar = aHasShift ? '$' : '4'; return true;
		case SDLK_5: theChar = aHasShift ? '%' : '5'; return true;
		case SDLK_6: theChar = aHasShift ? '^' : '6'; return true;
		case SDLK_7: theChar = aHasShift ? '&' : '7'; return true;
		case SDLK_8: theChar = aHasShift ? '*' : '8'; return true;
		case SDLK_9: theChar = aHasShift ? '(' : '9'; return true;
		case SDLK_0: theChar = aHasShift ? ')' : '0'; return true;
		case SDLK_MINUS: theChar = aHasShift ? '_' : '-'; return true;
		case SDLK_EQUALS: theChar = aHasShift ? '+' : '='; return true;
		case SDLK_LEFTBRACKET: theChar = aHasShift ? '{' : '['; return true;
		case SDLK_RIGHTBRACKET: theChar = aHasShift ? '}' : ']'; return true;
		case SDLK_BACKSLASH: theChar = aHasShift ? '|' : '\\'; return true;
		case SDLK_SEMICOLON: theChar = aHasShift ? ':' : ';'; return true;
		case SDLK_QUOTE: theChar = aHasShift ? '"' : '\''; return true;
		case SDLK_COMMA: theChar = aHasShift ? '<' : ','; return true;
		case SDLK_PERIOD: theChar = aHasShift ? '>' : '.'; return true;
		case SDLK_SLASH: theChar = aHasShift ? '?' : '/'; return true;
		case SDLK_BACKQUOTE: theChar = aHasShift ? '~' : '`'; return true;
		case SDLK_SPACE: theChar = ' '; return true;
		default: return false;
	}
}

void SexyAppBase::InitInput()
{
	SDL_Init(SDL_INIT_EVENTS);
}

static void RecordDemoMousePosition(SexyAppBase* theApp, int theX, int theY)
{
	theX &= 4095;
	theY &= 4095;

	int aDiffX = theX - theApp->mLastDemoMouseX;
	int aDiffY = theY - theApp->mLastDemoMouseY;

	if ((abs(aDiffX) < 32) && (abs(aDiffY) < 32))
	{
		if ((aDiffX != 0) || (aDiffY != 0))
		{
			theApp->WriteDemoTimingBlock();
			theApp->mDemoBuffer.WriteNumBits(1, 1);
			theApp->mDemoBuffer.WriteNumBits(0, 1);
			theApp->mDemoBuffer.WriteNumBits(aDiffX, 6);
			theApp->mDemoBuffer.WriteNumBits(aDiffY, 6);
		}
	}
	else
	{
		theApp->WriteDemoTimingBlock();
		theApp->mDemoBuffer.WriteNumBits(0, 1);
		theApp->mDemoBuffer.WriteNumBits(DEMO_MOUSE_POSITION, 5);
		theApp->mDemoBuffer.WriteNumBits(theX, 12);
		theApp->mDemoBuffer.WriteNumBits(theY, 12);
	}

	theApp->mLastDemoMouseX = theX;
	theApp->mLastDemoMouseY = theY;
}

static void RecordDemoEvent(SexyAppBase* theApp, const SDL_Event& theEvent)
{
	switch (theEvent.type)
	{
		case SDL_APP_WILLENTERBACKGROUND:
		case SDL_APP_DIDENTERFOREGROUND:
			theApp->WriteDemoTimingBlock();
			theApp->mDemoBuffer.WriteNumBits(0, 1);
			theApp->mDemoBuffer.WriteNumBits(DEMO_SIZE, 5);
			theApp->mDemoBuffer.WriteBoolean(theEvent.type == SDL_APP_WILLENTERBACKGROUND);
			break;

		case SDL_WINDOWEVENT:
			switch (theEvent.window.event)
			{
				case SDL_WINDOWEVENT_MINIMIZED:
				case SDL_WINDOWEVENT_RESTORED:
					theApp->WriteDemoTimingBlock();
					theApp->mDemoBuffer.WriteNumBits(0, 1);
					theApp->mDemoBuffer.WriteNumBits(DEMO_SIZE, 5);
					theApp->mDemoBuffer.WriteBoolean(theEvent.window.event == SDL_WINDOWEVENT_MINIMIZED);
					break;

				case SDL_WINDOWEVENT_FOCUS_GAINED:
				case SDL_WINDOWEVENT_FOCUS_LOST:
					theApp->WriteDemoTimingBlock();
					theApp->mDemoBuffer.WriteNumBits(0, 1);
					theApp->mDemoBuffer.WriteNumBits(DEMO_ACTIVATE_APP, 5);
					theApp->mDemoBuffer.WriteNumBits(theEvent.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ? 1 : 0, 1);
					break;

				case SDL_WINDOWEVENT_ENTER:
				case SDL_WINDOWEVENT_LEAVE:
				{
					bool aMouseIn = theEvent.window.event == SDL_WINDOWEVENT_ENTER;
					if (theApp->mMouseIn != aMouseIn)
					{
						theApp->WriteDemoTimingBlock();
						theApp->mDemoBuffer.WriteNumBits(0, 1);
						theApp->mDemoBuffer.WriteNumBits(aMouseIn ? DEMO_MOUSE_ENTER : DEMO_MOUSE_EXIT, 5);
					}
					break;
				}
			}
			break;

		case SDL_MOUSEMOTION:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		{
			int x = (theEvent.type == SDL_MOUSEMOTION) ? theEvent.motion.x : theEvent.button.x;
			int y = (theEvent.type == SDL_MOUSEMOTION) ? theEvent.motion.y : theEvent.button.y;
			theApp->mWidgetManager->RemapMouse(x, y);

			RecordDemoMousePosition(theApp, x, y);

			if (theEvent.type != SDL_MOUSEMOTION)
			{
				bool down = theEvent.type == SDL_MOUSEBUTTONDOWN;
				int aBtnNum =
					(theEvent.button.button == SDL_BUTTON_LEFT) ? 1 :
					(theEvent.button.button == SDL_BUTTON_RIGHT) ? -1 :
					3;
				if (down && theEvent.button.clicks == 2)
					aBtnNum = (theEvent.button.button == SDL_BUTTON_LEFT) ? 2 : -2;

				theApp->WriteDemoTimingBlock();
				theApp->mDemoBuffer.WriteNumBits(1, 1);
				theApp->mDemoBuffer.WriteNumBits(1, 1);
				theApp->mDemoBuffer.WriteNumBits(down ? 1 : 0, 1);
				theApp->mDemoBuffer.WriteNumBits(aBtnNum, 3);
			}

			if (!theApp->mMouseIn)
			{
				theApp->WriteDemoTimingBlock();
				theApp->mDemoBuffer.WriteNumBits(0, 1);
				theApp->mDemoBuffer.WriteNumBits(DEMO_MOUSE_ENTER, 5);
			}
			break;
		}

		case SDL_MOUSEWHEEL:
			theApp->WriteDemoTimingBlock();
			theApp->mDemoBuffer.WriteNumBits(0, 1);
			theApp->mDemoBuffer.WriteNumBits(DEMO_MOUSE_WHEEL, 5);
			theApp->mDemoBuffer.WriteNumBits(std::clamp(theEvent.wheel.y, -128, 127), 8);
			break;

		case SDL_KEYDOWN:
		{
			if (theApp->mAllowAltEnter &&
				theEvent.key.repeat == 0 &&
				(theEvent.key.keysym.sym == SDLK_RETURN || theEvent.key.keysym.sym == SDLK_KP_ENTER) &&
				(theEvent.key.keysym.mod & KMOD_ALT))
				break;

			KeyCode aCode = ((int)theEvent.key.keysym.scancode == WEBOS_SCANCODE_BACK || theEvent.key.keysym.sym == SDLK_AC_BACK)
				? KEYCODE_ESCAPE
				: SDLKeyToKeyCode(theEvent.key.keysym.sym);

			theApp->WriteDemoTimingBlock();
			theApp->mDemoBuffer.WriteNumBits(0, 1);
			theApp->mDemoBuffer.WriteNumBits(DEMO_KEY_DOWN, 5);
			theApp->mDemoBuffer.WriteNumBits(static_cast<int>(aCode), 8);

			char aChar = 0;
			if (SDLSynthesizeAsciiCharFromKeyDown(theEvent.key, aChar))
			{
				theApp->WriteDemoTimingBlock();
				theApp->mDemoBuffer.WriteNumBits(0, 1);
				theApp->mDemoBuffer.WriteNumBits(DEMO_KEY_CHAR, 5);
				theApp->mDemoBuffer.WriteNumBits(0, 1);
				theApp->mDemoBuffer.WriteNumBits(aChar, 8);
			}
			break;
		}

		case SDL_KEYUP:
		{
			KeyCode aCode = ((int)theEvent.key.keysym.scancode == WEBOS_SCANCODE_BACK || theEvent.key.keysym.sym == SDLK_AC_BACK)
				? KEYCODE_ESCAPE
				: SDLKeyToKeyCode(theEvent.key.keysym.sym);

			theApp->WriteDemoTimingBlock();
			theApp->mDemoBuffer.WriteNumBits(0, 1);
			theApp->mDemoBuffer.WriteNumBits(DEMO_KEY_UP, 5);
			theApp->mDemoBuffer.WriteNumBits(static_cast<int>(aCode), 8);
			break;
		}

		case SDL_TEXTINPUT:
			if (theEvent.text.text[0] != 0)
			{
				theApp->WriteDemoTimingBlock();
				theApp->mDemoBuffer.WriteNumBits(0, 1);
				theApp->mDemoBuffer.WriteNumBits(DEMO_KEY_TEXT, 5);
				theApp->mDemoBuffer.WriteString(theEvent.text.text);
			}
			break;
	}
}

bool SexyAppBase::StartTextInput([[maybe_unused]] std::string& theInput)
{
	SDL_StartTextInput();
	return false;
}

void SexyAppBase::StopTextInput()
{
	SDL_StopTextInput();
}

void SexyAppBase::SetTextInputRect(const Rect& theRect)
{
	const Rect& aLogical = mWidgetManager->mMouseDestRect;
	const Rect& aPresent = mWidgetManager->mMouseSourceRect;
	if (aLogical.mWidth <= 0 || aLogical.mHeight <= 0)
		return;

	SDL_Rect aRect;
	aRect.x = (theRect.mX - aLogical.mX) * aPresent.mWidth / aLogical.mWidth + aPresent.mX;
	aRect.y = (theRect.mY - aLogical.mY) * aPresent.mHeight / aLogical.mHeight + aPresent.mY;
	aRect.w = theRect.mWidth * aPresent.mWidth / aLogical.mWidth;
	aRect.h = theRect.mHeight * aPresent.mHeight / aLogical.mHeight;
	SDL_SetTextInputRect(&aRect);
}

bool SexyAppBase::ProcessDeferredMessages(bool singleMessage)
{
	SDL_Event event;
	if (SDL_PollEvent(&event))
	{
		if ((mRecordingDemoBuffer) && (!mShutdown))
			RecordDemoEvent(this, event);

		if (mPlayingDemoBuffer)
		{
			switch (event.type)
			{
				case SDL_QUIT:
					CloseRequestAsync();
					break;

				case SDL_WINDOWEVENT:
					if (event.window.event == SDL_WINDOWEVENT_CLOSE)
						CloseRequestAsync();
					else if (event.window.event == SDL_WINDOWEVENT_RESIZED)
					{
						mGLInterface->UpdateViewport();
						mWidgetManager->Resize(mScreenBounds, mGLInterface->mPresentationRect);
						mWidgetManager->MarkAllDirty();
					}
					break;
			}

			return SDL_HasEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
		}

		switch (event.type)
		{
			case SDL_QUIT:
				CloseRequestAsync();
				break;

			case SDL_APP_WILLENTERBACKGROUND:
				mMinimized = true;
				RehupFocus();
				break;

			case SDL_APP_DIDENTERFOREGROUND:
				mMinimized = false;
				RehupFocus();
				mWidgetManager->MarkAllDirty();
				break;

			case SDL_WINDOWEVENT:
				switch (event.window.event)
				{
					case SDL_WINDOWEVENT_CLOSE:
						CloseRequestAsync();
						break;

					case SDL_WINDOWEVENT_RESIZED:
						mGLInterface->UpdateViewport();
						mWidgetManager->Resize(mScreenBounds, mGLInterface->mPresentationRect);
						mWidgetManager->MarkAllDirty();
						break;

					case SDL_WINDOWEVENT_MINIMIZED:
						mMinimized = true;
						RehupFocus();
						break;

					case SDL_WINDOWEVENT_RESTORED:
						mMinimized = false;
						RehupFocus();
						mWidgetManager->MarkAllDirty();
						break;

					case SDL_WINDOWEVENT_FOCUS_GAINED:
					case SDL_WINDOWEVENT_FOCUS_LOST:
						mActive = event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED;
						RehupFocus();
						break;

					case SDL_WINDOWEVENT_ENTER:
						if (!mMouseIn)
						{
							int x, y;
							SDL_GetMouseState(&x, &y);
							mWidgetManager->RemapMouse(x, y);
							mMouseIn = true;
							mWidgetManager->MouseMove(x, y);
							EnforceCursor();
						}
						break;

					case SDL_WINDOWEVENT_LEAVE:
						if (mMouseIn)
						{
							mWidgetManager->MouseExit(mWidgetManager->mLastMouseX, mWidgetManager->mLastMouseY);
							mMouseIn = false;
							EnforceCursor();
						}
						break;
				}
				break;

			case SDL_MOUSEWHEEL:
			{
				mLastUserInputTick = mLastTimerTime;
				mWidgetManager->MouseWheel(event.wheel.y);
				break;
			}

			case SDL_MOUSEMOTION:
			{
				// Coalesce consecutive mouse motion events to avoid falling behind the Magic Remote / pointer event flood
				SDL_Event nextEvent;
				while (SDL_PeepEvents(&nextEvent, 1, SDL_PEEKEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0 &&
				       nextEvent.type == SDL_MOUSEMOTION)
				{
					SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
				}

				if (!mMouseIn)
					mMouseIn = true;

				int x = event.motion.x;
				int y = event.motion.y;
				mWidgetManager->RemapMouse(x, y);

				mLastUserInputTick = mLastTimerTime;

				mWidgetManager->MouseMove(x, y);
				break;
			}

			case SDL_MOUSEBUTTONDOWN:
			{
				if (!mMouseIn)
					mMouseIn = true;

				int x = event.button.x;
				int y = event.button.y;
				mWidgetManager->RemapMouse(x, y);

				mLastUserInputTick = mLastTimerTime;

				mWidgetManager->MouseMove(x, y);
				int btn =
					(event.button.button == SDL_BUTTON_LEFT) ? 1 :
					(event.button.button == SDL_BUTTON_RIGHT) ? -1 :
					3;
				if (event.button.clicks == 2)
					btn = (event.button.button == SDL_BUTTON_LEFT) ? 2 : -2;

				mWidgetManager->MouseDown(x, y, btn);
				break;
			}

			case SDL_MOUSEBUTTONUP:
			{
				if (!mMouseIn)
					mMouseIn = true;

				int x = event.button.x;
				int y = event.button.y;
				mWidgetManager->RemapMouse(x, y);

				mLastUserInputTick = mLastTimerTime;

				mWidgetManager->MouseMove(x, y);
				int btn =
					(event.button.button == SDL_BUTTON_LEFT) ? 1 :
					(event.button.button == SDL_BUTTON_RIGHT) ? -1 :
					3;

				mWidgetManager->MouseUp(x, y, btn);
				break;
			}

			case SDL_KEYDOWN:
			{
				mLastUserInputTick = mLastTimerTime;

				// webOS remote Back button
				if ((int)event.key.keysym.scancode == WEBOS_SCANCODE_BACK || event.key.keysym.sym == SDLK_AC_BACK)
				{
					mWidgetManager->KeyDown(KEYCODE_ESCAPE);
					break;
				}

				if (mAllowAltEnter &&
					event.key.repeat == 0 &&
					(event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) &&
					(event.key.keysym.mod & KMOD_ALT))
				{
					SwitchScreenMode(!mIsWindowed);
					break;
				}

				mWidgetManager->KeyDown(SDLKeyToKeyCode(event.key.keysym.sym));

				char aSynthesizedChar = 0;
				if (SDLSynthesizeAsciiCharFromKeyDown(event.key, aSynthesizedChar))
					mWidgetManager->KeyChar(aSynthesizedChar);

				break;
			}

			case SDL_KEYUP:
			{
				mLastUserInputTick = mLastTimerTime;

				// webOS remote Back button
				if ((int)event.key.keysym.scancode == WEBOS_SCANCODE_BACK || event.key.keysym.sym == SDLK_AC_BACK)
				{
					mWidgetManager->KeyUp(KEYCODE_ESCAPE);
					break;
				}

				mWidgetManager->KeyUp(SDLKeyToKeyCode(event.key.keysym.sym));
				break;
			}

			case SDL_TEXTINPUT:
				mLastUserInputTick = mLastTimerTime;
				mWidgetManager->KeyText(std::string_view(event.text.text));
				break;
		}
	}

	return SDL_HasEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
}
