#include "AcrylicCompositor.h"

AcrylicCompositor::AcrylicCompositor(HWND hwnd)
{
	InitDwmApi();
	CreateCompositionDevice();
	CreateEffectGraph(dcompDevice3);
}

bool AcrylicCompositor::SetAcrylicEffect(HWND hwnd, BackdropSource source, AcrylicEffectParameter params)
{
	fallbackColor = params.fallbackColor;
	tintColor = params.tintColor;
	if (source == BACKDROP_SOURCE_HOSTBACKDROP)
	{
		BOOL enable = TRUE;
		WINDOWCOMPOSITIONATTRIBDATA CompositionAttribute{};
		CompositionAttribute.Attrib = WCA_EXCLUDED_FROM_LIVEPREVIEW;
		CompositionAttribute.pvData = &enable;
		CompositionAttribute.cbData = sizeof(BOOL);
		DwmSetWindowCompositionAttribute(hwnd, &CompositionAttribute);
	}

	CreateBackdrop(hwnd,source);
	CreateCompositionVisual(hwnd);
	CreateFallbackVisual();
	mFallbackVisual->SetContent(swapChain.Get());
	mRootVisual->RemoveAllVisuals();

	SyncCoordinates(hwnd);
	switch (source)
	{
		case AcrylicCompositor::BACKDROP_SOURCE_DESKTOP:
			mRootVisual->AddVisual(mDesktopWindowVisual.Get(), false, NULL);
			mRootVisual->AddVisual(mFallbackVisual.Get(), true, mDesktopWindowVisual.Get());
			break;
		case AcrylicCompositor::BACKDROP_SOURCE_HOSTBACKDROP:
			mRootVisual->AddVisual(mDesktopWindowVisual.Get(), false, NULL);
			mRootVisual->AddVisual(mTopLevelWindowVisual.Get(), true, mDesktopWindowVisual.Get());
			mRootVisual->AddVisual(mFallbackVisual.Get(), true, mTopLevelWindowVisual.Get());
			break;
		default:
			mRootVisual->RemoveAllVisuals();
			break;
	}
	
	mRootVisual->SetClip(mClip.Get());
	mRootVisual->SetTransform(mTranslateTransform.Get());

	mSaturationEffect->SetSaturation(params.saturationAmount);

	mBlurEffect->SetBorderMode(D2D1_BORDER_MODE_HARD);
	mBlurEffect->SetInput(0, mSaturationEffect.Get(), 0);
	mBlurEffect->SetStandardDeviation(params.blurAmount);

	mRootVisual->SetEffect(mBlurEffect.Get());
	Commit();

	return true;
}

bool AcrylicCompositor::InitDwmApi()
{
	auto dwmapi = LoadLibrary(L"dwmapi.dll");
	auto user32 = LoadLibrary(L"user32.dll");

	if (!dwmapi || !user32)
	{
		return false;
	}

	DwmSetWindowCompositionAttribute = (SetWindowCompositionAttribute)GetProcAddress(user32, "SetWindowCompositionAttribute");
	DwmCreateSharedThumbnailVisual = (DwmpCreateSharedThumbnailVisual)GetProcAddress(dwmapi, MAKEINTRESOURCEA(147));
	DwmQueryWindowThumbnailSourceSize = (DwmpQueryWindowThumbnailSourceSize)GetProcAddress(dwmapi, MAKEINTRESOURCEA(162));
	DwmCreateSharedMultiWindowVisual = (DwmpCreateSharedMultiWindowVisual)GetProcAddress(dwmapi, MAKEINTRESOURCEA(163));
	DwmUpdateSharedMultiWindowVisual = (DwmpUpdateSharedMultiWindowVisual)GetProcAddress(dwmapi, MAKEINTRESOURCEA(164));
	
	return true;
}

bool AcrylicCompositor::CreateCompositionDevice()
{
	if (D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, d3d11Device.GetAddressOf(), nullptr, nullptr) != S_OK)
	{
		return false;
	}

	if (d3d11Device->QueryInterface(dxgiDevice.GetAddressOf()) != S_OK)
	{
		return false;
	}

	if (D2D1CreateFactory(D2D1_FACTORY_TYPE::D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory2), (void**)d2dFactory2.GetAddressOf()) != S_OK)
	{
		return false;
	}

	if (d2dFactory2->CreateDevice(dxgiDevice.Get(), d2Device.GetAddressOf()) != S_OK)
	{
		return false;
	}

	if(DCompositionCreateDevice3(dxgiDevice.Get(),__uuidof(dcompDevice),(void**)dcompDevice.GetAddressOf()) != S_OK)
	{
		return false;
	}

	if (dcompDevice->QueryInterface(__uuidof(IDCompositionDevice3), (LPVOID*)&dcompDevice3) != S_OK)
	{
		return false;
	}
	return true;
}

bool AcrylicCompositor::CreateFallbackVisual()
{
	description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	description.BufferCount = 2;
	description.SampleDesc.Count = 1;
	description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

	description.Width = GetSystemMetrics(SM_CXSCREEN);
	description.Height = GetSystemMetrics(SM_CYSCREEN);

	d3d11Device.As(&dxgiDevice);

	if (CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, __uuidof(dxgiFactory), reinterpret_cast<void**>(dxgiFactory.GetAddressOf())) != S_OK)
	{
		return false;
	}

	if (dxgiFactory->CreateSwapChainForComposition(dxgiDevice.Get(), &description, nullptr, swapChain.GetAddressOf()) != S_OK)
	{
		return false;
	}

	if (d2Device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, deviceContext.GetAddressOf()) != S_OK)
	{
		return false;
	}

	if (swapChain->GetBuffer(0, __uuidof(fallbackSurface), reinterpret_cast<void**>(fallbackSurface.GetAddressOf())) != S_OK)
	{
		return false;
	}

	properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
	properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
	properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
	if (deviceContext->CreateBitmapFromDxgiSurface(fallbackSurface.Get(), properties, fallbackBitmap.GetAddressOf()) != S_OK)
	{
		return false;
	}

	deviceContext->SetTarget(fallbackBitmap.Get());
	deviceContext->BeginDraw();
	deviceContext->Clear();
	deviceContext->CreateSolidColorBrush(tintColor, fallbackBrush.GetAddressOf());

	D2D1_ROUNDED_RECT roundRect{ fallbackRect, 20, 20 };
	deviceContext->FillRoundedRectangle(roundRect, fallbackBrush.Get());
	deviceContext->EndDraw();

	if (swapChain->Present(1, 0)!=S_OK)
	{
		return false;
	}

	return true;
}

bool AcrylicCompositor::CreateCompositionVisual(HWND hwnd)
{
	dcompDevice3->CreateVisual(&mRootVisual);
	dcompDevice3->CreateVisual(&mFallbackVisual);

	if (!CreateCompositionTarget(hwnd))
	{
		return false;
	}

	if (dcompTarget->SetRoot(mRootVisual.Get()) != S_OK)
	{
		return false;
	}

	return true;
}

bool AcrylicCompositor::CreateCompositionTarget(HWND hwnd)
{
	if (dcompDevice->CreateTargetForHwnd(hwnd, FALSE, dcompTarget.GetAddressOf()) != S_OK)
	{
		return false;
	}

	return true;
}

bool AcrylicCompositor::CreateBackdrop(HWND hwnd,BackdropSource source)
{
	switch (source)
	{
		case BACKDROP_SOURCE_DESKTOP:
			desktopWindow = (HWND)FindWindow(L"Progman", NULL);
			if (DwmQueryWindowThumbnailSourceSize(desktopWindow, FALSE, &thumbnailSize)!=S_OK)
			{
				return false;
			}
			thumbnail.dwFlags = DWM_TNP_SOURCECLIENTAREAONLY | DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_OPACITY | DWM_TNP_ENABLE3D;
			thumbnail.opacity = 255;
			thumbnail.fVisible = TRUE;
			thumbnail.fSourceClientAreaOnly = FALSE;
			thumbnail.rcDestination = RECT{ 0, 0, thumbnailSize.cx, thumbnailSize.cy };
			thumbnail.rcSource = RECT{ 0, 0, thumbnailSize.cx, thumbnailSize.cy };
			if (DwmCreateSharedThumbnailVisual(hwnd, desktopWindow, 2, &thumbnail, dcompDevice.Get(), (void**)mDesktopWindowVisual.GetAddressOf(), &desktopThumbnail) != S_OK)
			{
				return false;
			}
			break;
		case BACKDROP_SOURCE_HOSTBACKDROP:
			if (!CreateBackdrop(hwnd, BACKDROP_SOURCE_DESKTOP) 
				|| DwmCreateSharedMultiWindowVisual(hwnd, dcompDevice.Get(), (void**)mTopLevelWindowVisual.GetAddressOf(), &topLevelWindowThumbnail) != S_OK)
			{
				return false;
			}
			hwndExclusionList = new HWND[1];
			hwndExclusionList[0] = (HWND)0x0;
			if (DwmUpdateSharedMultiWindowVisual(topLevelWindowThumbnail, NULL, 0, hwndExclusionList, 1, &sourceRect, &destinationSize, 1) != S_OK)
			{
				return false;
			}
			break;
	}
	return true;
}

bool AcrylicCompositor::CreateEffectGraph(ComPtr<IDCompositionDevice3> dcompDevice3)
{
	if (dcompDevice3->CreateGaussianBlurEffect(mBlurEffect.GetAddressOf()) != S_OK)
	{
		return false;
	}
	if (dcompDevice3->CreateSaturationEffect(mSaturationEffect.GetAddressOf()) != S_OK)
	{
		return false;
	}
	if (dcompDevice3->CreateTranslateTransform(&mTranslateTransform) != S_OK)
	{
		return false;
	}
	if (dcompDevice3->CreateRectangleClip(&mClip) != S_OK)
	{
		return false;
	}
	return true;
}

void AcrylicCompositor::SyncCoordinates(HWND hwnd)
{
	GetWindowRect(hwnd, &hostWindowRect);
	mClip->SetLeft((float)hostWindowRect.left);
	mClip->SetRight((float)hostWindowRect.right);
	mClip->SetTop((float)hostWindowRect.top);
	mClip->SetBottom((float)hostWindowRect.bottom);

	mClip->SetTopLeftRadiusX(20);
	mClip->SetTopLeftRadiusY(20);
	mClip->SetTopRightRadiusX(20);
	mClip->SetTopRightRadiusY(20);

	mClip->SetBottomRightRadiusX(20);
	mClip->SetBottomRightRadiusY(20);
	mClip->SetBottomLeftRadiusX(20);
	mClip->SetBottomLeftRadiusY(20);

	mRootVisual->SetClip(mClip.Get());

	mTranslateTransform->SetOffsetX(-1 * (float)hostWindowRect.left);
	mTranslateTransform->SetOffsetY(-1 * (float)hostWindowRect.top);
	mRootVisual->SetTransform(mTranslateTransform.Get());
	Commit();
	DwmFlush();
}

bool AcrylicCompositor::Sync(HWND hwnd, int msg, WPARAM wParam, LPARAM lParam,bool active)
{
	switch (msg)
	{
		case WM_ACTIVATE:
			SyncFallbackVisual(active);
			Flush();
			return true;
		case WM_WINDOWPOSCHANGED:
			SyncCoordinates(hwnd);
			return true;
	}
	return false;
}

bool AcrylicCompositor::Flush()
{
	if (topLevelWindowThumbnail !=NULL)
	{
		DwmUpdateSharedMultiWindowVisual(topLevelWindowThumbnail, NULL, 0, hwndExclusionList, 1, &sourceRect, &destinationSize, 1);
		DwmFlush();
	}
	return false;
}

bool AcrylicCompositor::Commit()
{
	if (dcompDevice->Commit() != S_OK)
	{
		return false;
	}
	return true;
}

void AcrylicCompositor::SyncFallbackVisual(bool active)
{
	if (!active)
	{
		fallbackBrush->SetColor(fallbackColor);
	}
	else
	{
		fallbackBrush->SetColor(tintColor);
	}

	deviceContext->BeginDraw();
	deviceContext->Clear();

	D2D1_ROUNDED_RECT roundRect{ fallbackRect, 20, 20};
	deviceContext->FillRoundedRectangle(roundRect, fallbackBrush.Get());
	deviceContext->EndDraw();
	swapChain->Present(1, 0);
}
