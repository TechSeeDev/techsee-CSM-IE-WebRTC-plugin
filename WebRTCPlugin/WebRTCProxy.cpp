// WebRTCProxy.cpp : Implementation of WebRTCProxy
#include "stdafx.h"
#include <atlsafe.h>
#include "LogSinkImpl.h"

#include "JSObject.h"
#include "WebRTCProxy.h"
#include "RTCPeerConnection.h"
#include "MediaStreamTrack.h"

#include "rtc_base/ssl_adapter.h"
#include "api/create_peerconnection_factory.h"
#include "api/peer_connection_interface.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"

// Normal Device Capture
#include "modules/video_capture/video_capture.h"
#include "modules/video_capture/video_capture_factory.h"
#include "pc/video_track_source.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "VcmCapturer.hpp"
#include "VideoRenderer.h"

extern HINSTANCE g_hInstance;

bool WebRTCProxy::inited = false;
std::shared_ptr<rtc::Thread> WebRTCProxy::signalingThread;
std::shared_ptr<rtc::Thread> WebRTCProxy::eventThread;
std::shared_ptr<rtc::Thread> WebRTCProxy::workThread;
std::shared_ptr<rtc::Thread> WebRTCProxy::networkThread;

#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "media/engine/internal_encoder_factory.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {

class H264VideoEncoderFactory : public VideoEncoderFactory
{
public:
	H264VideoEncoderFactory()
		: internal_encoder_factory_(new InternalEncoderFactory()) {}

	VideoEncoderFactory::CodecInfo QueryVideoEncoder(
		const SdpVideoFormat& format) const override
	{
		// Format must be one of the internal formats.
		RTC_DCHECK(H264Encoder::IsSupported());
		VideoEncoderFactory::CodecInfo info;
		info.has_internal_source = false;
		info.is_hardware_accelerated = false;
		return info;
	}

	std::unique_ptr<VideoEncoder> CreateVideoEncoder(
		const SdpVideoFormat& format) override
	{
		// Try creating internal encoder.
		std::unique_ptr<VideoEncoder> internal_encoder;
		if (H264Encoder::IsSupported()) {
			cricket::VideoCodec vcodec = cricket::VideoCodec(cricket::kH264CodecName);
			internal_encoder = H264Encoder::Create(vcodec);
		}
		return internal_encoder;
	}

	std::vector<SdpVideoFormat> GetSupportedFormats() const override
	{
		return SupportedH264Codecs();
	}

private:
	const std::unique_ptr<VideoEncoderFactory> internal_encoder_factory_;
};
}

// WebRTCProxy
HRESULT WebRTCProxy::FinalConstruct()
{
	FUNC_BEGIN();

	if (!inited)
	{
		//rtc::LogMessage::ConfigureLogging("sensitive debug");
		rtc::LogMessage::ConfigureLogging("error");
		rtc::InitializeSSL();
		rtc::InitRandom(rtc::Time());
		rtc::ThreadManager::Instance()->WrapCurrentThread();

		signalingThread = std::shared_ptr<rtc::Thread>(rtc::Thread::Create().release());
		eventThread = std::shared_ptr<rtc::Thread>(rtc::Thread::Create().release());
		workThread = std::shared_ptr<rtc::Thread>(rtc::Thread::Create().release());
		networkThread = std::shared_ptr<rtc::Thread>(rtc::Thread::CreateWithSocketServer().release());

		signalingThread->SetName("signaling_thread", NULL);
		eventThread->SetName("event_thread", NULL);
		workThread->SetName("work_thread", NULL);
		networkThread->SetName("network_thread", NULL);

		if (!signalingThread->Start() || !eventThread->Start()
			|| !workThread->Start() || !networkThread->Start())
			FUNC_END_RET_S(false);

		// Initialize things on event thread
		eventThread->Invoke<void>(RTC_FROM_HERE, []() {
			CoInitializeEx(NULL, COINIT_MULTITHREADED /*COINIT_APARTMENTTHREADED*/);
		});

		inited = true;
	}

	//Create peer connection factory
	peer_connection_factory_ =
		webrtc::CreatePeerConnectionFactory(
			networkThread.get(),
			workThread.get(),
			signalingThread.get(),
			NULL,
			webrtc::CreateBuiltinAudioEncoderFactory(),
			webrtc::CreateBuiltinAudioDecoderFactory(),
			webrtc::CreateBuiltinVideoEncoderFactory(),
			//absl::make_unique<webrtc::H264VideoEncoderFactory>(),
			webrtc::CreateBuiltinVideoDecoderFactory(),
			NULL,
			NULL
		).release();

	//Check
	if (!peer_connection_factory_)
		FUNC_END_RET_S(S_FALSE);

	FUNC_END_RET_S(S_OK);
}
IUnknown* g_audioTrack = nullptr;

void WebRTCProxy::FinalRelease()
{
	FUNC_BEGIN();

	if (g_audioTrack)
		g_audioTrack->Release();
	g_audioTrack = nullptr;

	// Remove factory
	if (peer_connection_factory_)
		peer_connection_factory_->Release();
	peer_connection_factory_ = nullptr;

	if (inited)
	{
		eventThread->Invoke<void>(RTC_FROM_HERE, []() {
			CoUninitialize();
		});

		networkThread->Quit();

		workThread->Quit();
		eventThread->Quit();
		signalingThread->Quit();

		rtc::CleanupSSL();
		rtc::Thread::Current()->Quit();
		inited = false;
	}


	FUNC_END();
}

STDMETHODIMP WebRTCProxy::createPeerConnection(VARIANT variant, IUnknown** peerConnection)
{
	FUNC_BEGIN();

	webrtc::PeerConnectionInterface::RTCConfiguration configuration;
	JSObject obj(variant);

	if (!obj.isNull())
	{
		/*
		dictionary RTCIceServer {
		  required (DOMString or sequence<DOMString>) urls;
		  DOMString                          username;
		  (DOMString or RTCOAuthCredential)  credential;
		  RTCIceCredentialType               credentialType = "password";
		};

		dictionary RTCConfiguration {
		  sequence<RTCIceServer>   iceServers;
		  RTCIceTransportPolicy    iceTransportPolicy = "all";
		  RTCBundlePolicy          bundlePolicy = "balanced";
		  RTCRtcpMuxPolicy         rtcpMuxPolicy = "require";
		  DOMString                peerIdentity;
		  sequence<RTCCertificate> certificates;
		  [EnforceRange]
		  octet                    iceCandidatePoolSize = 0;
		  */
		  //Get ice servers array
		JSObject iceServers = obj.GetProperty(L"iceServers");
		//TODO: support the following ones
		_bstr_t bundlePolicy = obj.GetStringProperty(L"bundlePolicy");
		_bstr_t rtcpMuxPolicy = obj.GetStringProperty(L"rtcpMuxPolicy");
		//_bstr_t peerIdentity      = obj.GetStringProperty(L"peerIdentity");
		int iceCandidatePoolSize = obj.GetIntegerProperty(L"iceServers");

		//If we have them
		if (!iceServers.isNull())
		{
			//For each property
			for (auto name : iceServers.GetPropertyNames())
			{
				//Get ice server
				JSObject server = iceServers.GetProperty(name);
				//If we have it
				if (!server.isNull())
				{
					webrtc::PeerConnectionInterface::IceServer iceServer;

					//Get the values
					auto urls = server.GetProperty(L"urls");
					_bstr_t username = server.GetStringProperty(L"username");
					_bstr_t credential = server.GetStringProperty(L"credential");
					//TODO: Support credential type
					_bstr_t credentialType = server.GetStringProperty(L"credentialType"); //Not supported yet
					//if url is an string
					if (urls.vt == VT_BSTR)
					{
						//Get url
						_bstr_t url(urls.bstrVal);
						//Append
						iceServer.urls.push_back((char*)url);
					}
					else {
						//Convert to object
						JSObject aux(urls);
						//Ensure we have it
						if (!aux.isNull())
						{
							//Get all urls
							for (auto idx : aux.GetPropertyNames())
							{
								//Get url
								_bstr_t url = aux.GetStringProperty(idx);
								//Append
								iceServer.urls.push_back((char*)url);
							}
						}
					}
					//Set username and credential, OATH not supported yet
					if ((char*)username)
						iceServer.username = (char*)username;
					if ((char*)credential)
						iceServer.password = (char*)credential;
					//Push
					configuration.servers.push_back(iceServer);
				}
			}
		}
	};

	//Create activeX object which is a
	CComObject<RTCPeerConnection>* pc;
	HRESULT hresult = CComObject<RTCPeerConnection>::CreateInstance(&pc);

	if (FAILED(hresult))
		FUNC_END_RET_S(hresult);

	//Create peerconnection object, it will call the AddRef inside as it gets a ref to the observer
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> pci = peer_connection_factory_->CreatePeerConnection(
		configuration,
		NULL,
		NULL,
		pc
	);

	//Check it was created correctly
	if (!pci)
		//Error
		FUNC_END_RET_S(E_INVALIDARG);

	//Set event thread
	pc->SetThread(eventThread);

	//Attach to PC
	pc->Attach(pci);

	//Get Reference to pass it to JS
	*peerConnection = pc->GetUnknown();

	//Add JS reference
	(*peerConnection)->AddRef();

	//OK
	FUNC_END_RET_S(hresult);
}


STDMETHODIMP WebRTCProxy::createLocalAudioTrack(VARIANT constraints, IUnknown** track)
{
	FUNC_BEGIN();

	const cricket::AudioOptions options;
	//Create audio source
	auto audioSource = peer_connection_factory_->CreateAudioSource(options);

	//Ensure it is created
	if (!audioSource)
		FUNC_END_RET_S(E_UNEXPECTED);

	//Create track
	rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> audioTrack = peer_connection_factory_->CreateAudioTrack("audio", audioSource);

	//Ensure it is created
	if (!audioTrack)
		FUNC_END_RET_S(E_UNEXPECTED);

	//Create activeX object which is a
	CComObject<MediaStreamTrack>* mediaStreamTrack;
	HRESULT hresult = CComObject<MediaStreamTrack>::CreateInstance(&mediaStreamTrack);

	if (FAILED(hresult))
		FUNC_END_RET_S(hresult);

	//Attach to native track
	mediaStreamTrack->Attach(audioTrack);

	//Set dummy audio  label
	mediaStreamTrack->SetLabel("Default Audio Device");

	//Get Reference to pass it to JS
	*track = mediaStreamTrack->GetUnknown();

	//Add JS reference
	(*track)->AddRef();
	
	g_audioTrack = *track;

	//OK
	FUNC_END_RET_S(hresult);
}

class CapturerTrackSource : public webrtc::VideoTrackSource
{
public:
	static rtc::scoped_refptr<CapturerTrackSource> Create()
	{
		const size_t kWidth = 640;
		const size_t kHeight = 480;
		const size_t kFps = 30;
		const size_t kDeviceIndex = 0;
		std::unique_ptr<VcmCapturer> capturer =
			absl::WrapUnique(VcmCapturer::Create(kWidth, kHeight, kFps, kDeviceIndex));
		if (!capturer)
		{
			return nullptr;
		}
		return new rtc::RefCountedObject<CapturerTrackSource>(std::move(capturer));
	}

protected:
	explicit CapturerTrackSource(std::unique_ptr<VcmCapturer> capturer)
		: VideoTrackSource(/*remote=*/false), capturer(std::move(capturer))
	{
	}

private:
	rtc::VideoSourceInterface<webrtc::VideoFrame>* source() override
	{
		return capturer.get();
	}

public:
	std::unique_ptr<VcmCapturer> capturer;
};

STDMETHODIMP WebRTCProxy::createLocalVideoTrack(VARIANT constraints, IUnknown** track)
{
	FUNC_BEGIN();

	//Create the video source from capture, note that the video source keeps the std::unique_ptr of the videoCapturer
	auto captureSource = CapturerTrackSource::Create();
	rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> videoSource = captureSource;
	if (!videoSource)
		FUNC_END_RET_S(E_UNEXPECTED);

	//Now create the track
	rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> videoTrack =
		peer_connection_factory_->CreateVideoTrack("video", videoSource);
	if (!videoTrack)
		FUNC_END_RET_S(E_UNEXPECTED);

	//Create activeX object for media stream track
	CComObject<MediaStreamTrack>* mediaStreamTrack;
	HRESULT hresult = CComObject<MediaStreamTrack>::CreateInstance(&mediaStreamTrack);
	if (FAILED(hresult))
		FUNC_END_RET_S(hresult);

	//Attach to native track
	mediaStreamTrack->Attach(videoTrack);

	//Set device name as label
	mediaStreamTrack->SetLabel(captureSource->capturer->label);

	//Get Reference to pass it to JS
	*track = mediaStreamTrack->GetUnknown();

	//Add JS reference
	(*track)->AddRef();

	//OK
	FUNC_END_RET_S(hresult);
}


STDMETHODIMP WebRTCProxy::parseIceCandidate(VARIANT candidate, VARIANT* parsed)
{
	FUNC_BEGIN();

	//Check input is a string
	if (candidate.vt != VT_BSTR)
		FUNC_END_RET_S(E_INVALIDARG);

	//Get candidate as string
	std::string str = (char*)_bstr_t(candidate);

	//Try to parse input
	webrtc::SdpParseError parseError;
	// Creates a IceCandidateInterface based on SDP string.
	std::unique_ptr<webrtc::IceCandidateInterface> iceCandidate(webrtc::CreateIceCandidate("audio", 0, str, &parseError));

	if (!iceCandidate)
		FUNC_END_RET_S(E_INVALIDARG);

	//Fill data
	_variant_t foundation = iceCandidate->candidate().foundation().c_str();
	_variant_t component = iceCandidate->candidate().component();
	_variant_t priority = iceCandidate->candidate().priority();
	_variant_t ip = iceCandidate->candidate().address().hostname().c_str();
	_variant_t protocol = iceCandidate->candidate().protocol().c_str();
	_variant_t port = iceCandidate->candidate().address().port();
	_variant_t type = iceCandidate->candidate().type().c_str();
	_variant_t tcpType = iceCandidate->candidate().tcptype().c_str();
	_variant_t relatedAddress = iceCandidate->candidate().related_address().hostname().c_str();
	_variant_t relatedPort = iceCandidate->candidate().related_address().port();
	_variant_t usernameFragment = iceCandidate->candidate().username().c_str();

	CComSafeArray<VARIANT> args(11);
	args.SetAt(0, foundation);
	args.SetAt(1, component);
	args.SetAt(2, priority);
	args.SetAt(3, ip);
	args.SetAt(4, protocol);
	args.SetAt(5, port);
	args.SetAt(6, type);
	args.SetAt(7, tcpType);
	args.SetAt(8, relatedAddress);
	args.SetAt(9, relatedPort);
	args.SetAt(10, usernameFragment);

	// Initialize the variant
	VariantInit(parsed);
	parsed->vt = VT_ARRAY | VT_VARIANT;
	parsed->parray = args.Detach();

	//Parsed ok
	FUNC_END_RET_S(S_OK);
}

STDMETHODIMP WebRTCProxy::getVersion(VARIANT* retVal)
{
	FUNC_BEGIN();

	HRSRC rcsrc = FindResource(g_hInstance, MAKEINTRESOURCE(IDR_WEBRTCPROXY), L"REGISTRY");
	if (!rcsrc)
		FUNC_END_RET_S(E_FAIL);

	HGLOBAL hRrsc = LoadResource(g_hInstance, rcsrc);
	if (!hRrsc)
		FUNC_END_RET_S(E_FAIL);

	void* lpMemRes = LockResource(hRrsc);
	if (!lpMemRes)
		FUNC_END_RET_S(E_FAIL);

	char* lpBuff = (char*)lpMemRes;
	if (!lpBuff)
		FUNC_END_RET_S(E_FAIL);

	// Version = s '1.0'
	ATL::CStringA rgs = lpBuff;
	int pos1 = rgs.Find("Version = s");
	if (pos1 <= 0)
		FUNC_END_RET_S(E_FAIL);

	int pos2 = rgs.Find("'", pos1);
	if (pos2 <= 0)
		FUNC_END_RET_S(E_FAIL);

	int pos3 = rgs.Find("'", pos2 + 1);
	if (pos3 <= 0)
		FUNC_END_RET_S(E_FAIL);

	rgs = rgs.Mid(pos2 + 1, pos3 - (pos2 + 1));
	variant_t outVal = rgs.GetString();
	(*retVal).vt = VT_BSTR;
	(*retVal).bstrVal = SysAllocString(outVal.bstrVal);

	FUNC_END_RET_S(S_OK);
}

STDMETHODIMP WebRTCProxy::setLogFilePath(VARIANT path, int severity /*= rtc::LS_VERBOSE*/)
{
	if (path.vt != VT_BSTR)
		FUNC_END_RET_S(E_FAIL);

	if (g_logSink)
	{
		rtc::LogMessage::RemoveLogToStream(g_logSink);
		delete g_logSink;
	}

	ATL::CStringA sPath;
	sPath.AppendFormat("%ws", path.bstrVal);

	ATL::CStringA s;
	s.AppendFormat("Path:%ws - severity: %d", path.bstrVal, severity);
	OutputDebugStringA(s.GetString());
	

	g_logSink = new LogSinkImpl(sPath.GetString(), (rtc::LoggingSeverity)severity);
	rtc::LogMessage::AddLogToStream(g_logSink, (rtc::LoggingSeverity)severity);
	rtc::LogMessage::LogToDebug((rtc::LoggingSeverity)severity);
	
	RTC_LOG_F(LS_VERBOSE) << "A new log sink has been created.";

	//RTC_LOG_DEBUG("A new log sink has been created.\n");

	FUNC_END_RET_S(S_OK);
}