#ifndef UNICODE
#define UNICODE
#endif 

#include "Acrylic Window.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	const wchar_t CLASS_NAME[] = L"Acrylic Window";

	WNDCLASS wc = { };
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;
	RegisterClass(&wc);

	HWND hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, CLASS_NAME, L"Acrylic Window using Direct Composition",
		WS_POPUP, 100, 200, 800, 600, NULL, NULL, hInstance, NULL);
	// SetWindowLong(hwnd, GWL_STYLE, GetWindowLong(hwnd, GWL_STYLE) & ~WS_CAPTION);
	if (hwnd == NULL)
	{
		return 0;
	}

	ShowWindow(hwnd, SW_SHOW);

	compositor.reset(new AcrylicCompositor(hwnd));

	AcrylicCompositor::AcrylicEffectParameter param;
	param.blurAmount = 40;
	param.saturationAmount = 1;
	param.tintColor = D2D1::ColorF(0.0f, 0.0f, 0.0f, .30f);
	param.fallbackColor = D2D1::ColorF(0.10f,0.10f,0.10f,1.0f);

	compositor->SetAcrylicEffect(hwnd, AcrylicCompositor::BACKDROP_SOURCE_HOSTBACKDROP, param);

	MSG msg = { };
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (compositor)
	{
		if (uMsg == WM_ACTIVATE)
		{
			if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam)==WA_CLICKACTIVE)
			{
				active = true;
			}
			else if (LOWORD(wParam) == WA_INACTIVE)
			{
				active = false;
			}
		}
		compositor->Sync(hwnd, uMsg, wParam, lParam,active);
	}

	switch (uMsg)
	{
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		default:
			break;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
