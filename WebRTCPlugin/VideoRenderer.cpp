// VideoRenderer.cpp : Implementation of VideoRenderer
#include "stdafx.h"
#include "WebRTCProxy.h"
#include "VideoRenderer.h"
#undef FOURCC
#include "third_party/libyuv/include/libyuv.h"
#include "api/video/i420_buffer.h"


HRESULT VideoRenderer::FinalConstruct()
{
	SetThread(WebRTCProxy::GetEventThread());
	rotation = (webrtc::VideoRotation)-1;
	videoWidth = videoHeight = 0;

	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

	// Done
	return S_OK;
}

void VideoRenderer::OnFrame(const webrtc::VideoFrame& frame)
{
	// Check if size has changed
	if ((videoWidth != frame.width() || videoHeight != frame.height()) ||
		frame.rotation() != rotation)
	{
		if (frame.rotation() != rotation)
		{
			RTC_LOG(LS_INFO) << "frame rotation changed to " << frame.rotation();
			rotation = frame.rotation();
		}

		// Update
		if (frame.rotation() == webrtc::kVideoRotation_90)
		{
			videoWidth = frame.height();
			videoHeight = frame.width();
		}
		else /*webrtc::kVideoRotation_0 || webrtc::kVideoRotation_180)*/
		{
			videoWidth = frame.width();
			videoHeight = frame.height();
		}

		// Fire event
		variant_t width = videoWidth;
		variant_t height = videoHeight;
		DispatchAsync(onresize, width, height);
	}

	// Only iOS rotated frames need to be adjusted.
	webrtc::VideoFrame* clone = nullptr;
	if (rotation == webrtc::kVideoRotation_90 ||
		rotation == webrtc::kVideoRotation_180)
	{
		// Apply rotation and clone video frame
		webrtc::VideoFrame rotated_frame(frame);
		rotated_frame.set_video_frame_buffer(
				webrtc::I420Buffer::Rotate(*frame.video_frame_buffer()->GetI420(),
				frame.rotation()));
		rotated_frame.set_rotation(webrtc::kVideoRotation_0);
		clone = new webrtc::VideoFrame(rotated_frame);
	}
	else
		clone = new webrtc::VideoFrame(frame);

	// Store clone as background frame
	mutex.lock();

	frames[background] = std::shared_ptr<webrtc::VideoFrame>(clone);

	// Move background buffer to foreground
	background = !background;

	mutex.unlock();
	
	// Redraw
	::InvalidateRect(hwndParent, NULL, 0);
}

void VideoRenderer::OnDiscardedFrame()
{
	RTC_LOG(LS_WARNING) << "A frame has been discarded.";
}

HRESULT VideoRenderer::OnDrawAdvanced(ATL_DRAWINFO& di)
{
	RECT* rc = (RECT*)di.prcBounds;
	HDC hdc = di.hdcDraw;

	// Prevent any initial resizing flicker
	if (rotation != -1)
	{
		// Rotation recalculation precaution
		rc->left = rc->left;
		rc->top = rc->top;
		rc->right = rc->left + videoWidth;
		rc->bottom = rc->top + videoHeight;
	}

	// Create black brush
	HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));

	// Fill rect with black
	FillRect(hdc, rc, hBrush);

	// Delete black brush
	DeleteObject(hBrush);

	// Lock
	mutex.lock();

	// Get foreground buffer
	std::shared_ptr<webrtc::VideoFrame> frame(frames[!background]);

	// Unlock
	mutex.unlock();

	// Check if we have a frame already
	if (!frame)
		return S_OK;

	// Get width and height
	int width = frame->width();
	int height = frame->height();

	// Get I420 data
	auto yuv = frame->video_frame_buffer()->ToI420();

	// Create ARGB data
	uint8_t* rgb = (uint8_t*)malloc(width * height * 4);

	// Convert to rgb
	libyuv::I420ToARGB(
		yuv->DataY(),
		yuv->StrideY(),
		yuv->DataU(),
		yuv->StrideU(),
		yuv->DataV(),
		yuv->StrideV(),
		rgb,
		width * 4,
		width,
		height);

	// Set stretching mode
	int oldMode = SetStretchBltMode(hdc, HALFTONE);

	// Create bitmap
	BITMAPINFO info;
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;;
	info.bmiHeader.biHeight = height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	info.bmiHeader.biSizeImage = 0;
	info.bmiHeader.biXPelsPerMeter = 0;
	info.bmiHeader.biYPelsPerMeter = 0;
	info.bmiHeader.biClrUsed = 0;
	info.bmiHeader.biClrImportant = 0;

	// Fill rect
	int xDest = rc->left;
	int yDest = rc->bottom;
	int wDest = rc->right - rc->left;
	int hDest = rc->top - rc->bottom;

	// Copy & stretch
	StretchDIBits(
		hdc,
		xDest,
		yDest,
		wDest,
		hDest,
		0,
		0,
		width,
		height,
		rgb,
		&info,
		DIB_RGB_COLORS,
		SRCCOPY
	);

	// Clean rgb data
	free(rgb);
	
	// Restore stretching mode
	SetStretchBltMode(hdc, oldMode);

	return S_OK;
}

STDMETHODIMP VideoRenderer::setTrack(VARIANT track)
{
	//Get dispatch interface
	if (track.vt != VT_DISPATCH)
		return E_INVALIDARG;

	IDispatch* disp = V_DISPATCH(&track);
	if (!disp)
		return E_INVALIDARG;

	//Get atl com object from track.
	CComPtr<ITrackAccess> proxy;
	HRESULT hr = disp->QueryInterface(IID_PPV_ARGS(&proxy));
	if (FAILED(hr))
		return hr;

	//Convert to video
	webrtc::VideoTrackInterface* videoTrack = reinterpret_cast<webrtc::VideoTrackInterface*>(proxy->GetTrack().get());
	if (!videoTrack)
		return E_INVALIDARG;

	//Add us as video 
	rtc::VideoSinkWants wanted;
	wanted.rotation_applied = true;
	videoTrack->AddOrUpdateSink(this, wanted);

	return S_OK;
}

//////////////////////////////////////////////////////////////////////////

static const std::string base64_chars =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(BYTE const* bytes_to_encode, unsigned int in_len)
{
	std::string ret;
	int i = 0;
	int j = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];

	while (in_len--)
	{
		char_array_3[i++] = *(bytes_to_encode++);
		if (i == 3)
		{
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (i = 0; (i < 4); i++)
				ret += base64_chars[char_array_4[i]];
			i = 0;
		}
	}

	if (i)
	{
		for (j = i; j < 3; j++)
			char_array_3[j] = '\0';

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;

		for (j = 0; (j < i + 1); j++)
			ret += base64_chars[char_array_4[j]];

		while ((i++ < 3))
			ret += '=';
	}

	return ret;
}

//////////////////////////////////////////////////////////////////////////

bool GetEncoderClsid(std::wstring format, CLSID* pClsid)
{
	UINT num = 0;          // number of image encoders
	UINT size = 0;         // size of the image encoder array in bytes

	Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;

	Gdiplus::GetImageEncodersSize(&num, &size);
	if (size == 0)
		return false;

	pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
	if (pImageCodecInfo == NULL)
		return false;

	GetImageEncoders(num, size, pImageCodecInfo);
	
	for (UINT j = 0; j < num; ++j)
	{
		if (_wcsicmp(pImageCodecInfo[j].MimeType, format.c_str()) == 0)
		{
			*pClsid = pImageCodecInfo[j].Clsid;
			free(pImageCodecInfo);
			return true;
		}
	}

	free(pImageCodecInfo);
	return false;
}

STDMETHODIMP VideoRenderer::getFrame(VARIANT* val)
{
	HRESULT hr = S_OK;

	// Get foreground buffer
	mutex.lock();
	std::shared_ptr<webrtc::VideoFrame> frame(frames[false]);
	mutex.unlock();

	// Check if we have a frame already
	if (!frame)
		return S_OK;

	// Get width and height
	int width = videoWidth;
	int height = videoHeight;

	int imageSize = width * height * 4;

	// Get I420 data
	auto yuv = frame->video_frame_buffer()->ToI420();

	uint8_t* rgba = (uint8_t*)malloc(imageSize);
	memset(rgba, 0x0, imageSize);

	// Convert to rgba
	libyuv::I420ToARGB(
		yuv->DataY(),
		yuv->StrideY(),
		yuv->DataU(),
		yuv->StrideU(),
		yuv->DataV(),
		yuv->StrideV(),
		rgba,
		width * 4,
		width,
		height);

	//////////////////////////////////////////////////////////////////////////

	IStream* pStream = nullptr;
	Gdiplus::Bitmap* bm = nullptr;

	CLSID clsid;
	if (GetEncoderClsid(L"image/png", &clsid))
	{
		bm = new Gdiplus::Bitmap(width, height, width * 4,
			PixelFormat32bppARGB, rgba);
#ifdef _DEBUG
		bm->Save(L"C:\\test.png", &clsid, nullptr);
#endif // _DEBUG
		
		hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
		if (FAILED(hr))
		{
			RTC_LOG(LS_INFO) << "failed to create snapshot stream";
			goto clean_up;
		}

		auto st = bm->Save(pStream, &clsid);
		if (st != Gdiplus::Ok)
		{
			RTC_LOG(LS_INFO) << "failed to save snapshot to stream";
			goto clean_up;
		}

		ULARGE_INTEGER liSize = {0};
		hr = IStream_Size(pStream, &liSize);
		if (FAILED(hr))
		{
			RTC_LOG(LS_INFO) << "failed to get stream size";
			goto clean_up;
		}

		ULARGE_INTEGER pos = { 0 };
		LARGE_INTEGER liPos = { 0 };
		hr = pStream->Seek(liPos, STREAM_SEEK_SET, &pos);
		if (FAILED(hr))
		{
			RTC_LOG(LS_INFO) << "failed to set stream read position";
			goto clean_up;
		}

		// Copy the stream into buffer
		BYTE* strmBuff = (BYTE*)malloc(liSize.QuadPart);
		memset(strmBuff, 0x0, liSize.QuadPart);
		
		ULONG bytesRead = 0;
		hr = pStream->Read(strmBuff, liSize.QuadPart, &bytesRead);
		if (SUCCEEDED(hr))
		{
#ifdef _DEBUG
			FILE* fp = fopen("C:\\test2.png", "wb");
			if (fp != NULL)
			{
				fwrite(strmBuff, bytesRead, 1, fp);
				fclose(fp);
			}
#endif // _DEBUG

			std::string base64Bitmap = base64_encode(strmBuff, bytesRead);
			if (!base64Bitmap.empty() && val != nullptr)
			{
				variant_t out = base64Bitmap.c_str();
				(*val).vt = VT_BSTR;
				(*val).bstrVal = SysAllocString(out.bstrVal);
			}
		}
		free(strmBuff);
	}
	else
		RTC_LOG(LS_INFO) << "failed to create snapshot encoder";

clean_up:
	if (pStream)
		pStream->Release();

	if (bm)
		delete bm;

	// Clean rgba data
	free(rgba);

	return hr;
}
