#include "stdafx.h"
#include "LogSinkImpl.h"
#include "VcmCapturer.hpp"

#include <memory>
#include <stdint.h>

#include "modules/video_capture/video_capture_factory.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

VcmCapturer::VcmCapturer() : vcm_(nullptr)
{
}

bool VcmCapturer::Init(size_t width, size_t height, size_t target_fps, size_t capture_device_index)
{
	FUNC_BEGIN();

	std::vector<std::string> device_ids;
	std::vector<std::string> device_names;
	std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> device_info(webrtc::VideoCaptureFactory::CreateDeviceInfo());

	// Check there is no error
	if (!device_info)
	{
		FUNC_END();
		return false;
	}

	// Get all names for devices
	int num_devices = device_info->NumberOfDevices();
	for (int i = 0; i < num_devices; ++i)
	{
		const uint32_t kSize = 256;
		char name[kSize] = { 0 };
		char id[kSize] = { 0 };
		if (device_info->GetDeviceName(i, name, kSize, id, kSize) != -1)
		{
			device_ids.push_back(id);
			device_names.push_back(name);
		}
	}

	// Try all 
	for (const auto& id : device_ids)
	{
		// Open capturer
		vcm_ = webrtc::VideoCaptureFactory::Create(id.c_str());
		if (vcm_)
		{
			id_ = id;
			label = id_;
			break;
		}
	}

	// Ensure it is created
	if (!vcm_)
	{
		FUNC_END();
		return false;
	}

	vcm_->RegisterCaptureDataCallback(this);

	device_info->GetCapability(vcm_->CurrentDeviceName(), 0, capability_);

	capability_.width     = static_cast<int32_t>(width);
	capability_.height    = static_cast<int32_t>(height);
	capability_.maxFPS    = static_cast<int32_t>(target_fps);
	capability_.videoType = webrtc::VideoType::kI420;

	if (vcm_->StartCapture(capability_) != 0)
	{
		Destroy();
		FUNC_END();
		return false;
	}

	RTC_CHECK(vcm_->CaptureStarted());

	FUNC_END();

	return true;
}

VcmCapturer* VcmCapturer::Create(size_t width, size_t height, size_t target_fps, size_t capture_device_index)
{
	FUNC_BEGIN();

	std::unique_ptr<VcmCapturer> vcm_capturer(new VcmCapturer());
	if (!vcm_capturer->Init(width, height, target_fps, capture_device_index))
	{
		RTC_LOG(LS_WARNING) << "Failed to create VcmCapturer(w = " << width << ", h = " << height
		                    << ", fps = " << target_fps << ")";
		FUNC_END();
		return nullptr;
	}
	FUNC_END();

	return vcm_capturer.release();
}

void VcmCapturer::Destroy()
{
	FUNC_BEGIN();

	if (!vcm_)
		return;

	vcm_->StopCapture();
	vcm_->DeRegisterCaptureDataCallback();
	// Release reference to VCM.
	vcm_ = nullptr;

	FUNC_END();
}

VcmCapturer::~VcmCapturer()
{
	FUNC_BEGIN();

	Destroy();

	FUNC_END();
}

void VcmCapturer::OnFrame(const webrtc::VideoFrame& frame)
{
	FUNC_BEGIN();

	VideoCapturer::OnFrame(frame);

	FUNC_END();
}
