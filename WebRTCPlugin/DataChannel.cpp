// DataChannel.cpp : Implementation of DataChannel
#include "stdafx.h"
#include "LogSinkImpl.h"
#include "DataChannel.h"
#include "JSObject.h"

STDMETHODIMP DataChannel::send(VARIANT data)
{
	FUNC_BEGIN();

	if (data.vt == !VT_BSTR)
		FUNC_END_RET_S(E_INVALIDARG);

	//Get string
	std::string message = (char*)_bstr_t(data);

	//Send it
	if (!datachannel->Send(webrtc::DataBuffer(message)))
		//Error
		FUNC_END_RET_S(E_FAIL);

	//Done
	FUNC_END_RET_S(S_OK);
}


STDMETHODIMP DataChannel::close()
{
	FUNC_BEGIN();

	datachannel->Close();
	FUNC_END_RET_S(S_OK);
}


// The data channel state have changed.
void DataChannel::OnStateChange()
{
	FUNC_BEGIN();

	//Check state
	switch (datachannel->state())
	{
	case webrtc::DataChannelInterface::kConnecting:
		FUNC_END();
		return;
	case webrtc::DataChannelInterface::kOpen:
		DispatchAsync(onopen);
		FUNC_END();
		return;
	case webrtc::DataChannelInterface::kClosing:
		FUNC_END();
		return;
	case webrtc::DataChannelInterface::kClosed:
		DispatchAsync(onclose);
		FUNC_END();
		return;
	}
	FUNC_END();
}

void DataChannel::OnMessage(const webrtc::DataBuffer& buffer)
{
	FUNC_BEGIN();

	//If we still don't have the handler
	if (onmessage.IsSet())
	{
		//Check buffer type
		if (!buffer.binary)
		{
			//Create string
			std::string message((const char*)buffer.data.data(), buffer.size());
			//Event
			DispatchAsync(onmessage, message);
		}
	}
	else {
		//Enqueue
		pending.push_back(buffer);
	}
	
	FUNC_END();
}

void DataChannel::OnBufferedAmountChange(uint64_t previous_amount)
{
	FUNC_BEGIN();

	//Check buffered ammount is below threshold
	if (datachannel->buffered_amount() < bufferedAmountLowThreshold)
		//Fire event
		DispatchAsync(onbufferedamountlow);
	FUNC_END();
}


STDMETHODIMP DataChannel::get_label(VARIANT* val)
{
	FUNC_BEGIN();

	variant_t variant = this->datachannel->label().c_str();
	*val = variant;
	FUNC_END_RET_S(S_OK);
}
STDMETHODIMP DataChannel::get_ordered(VARIANT* val)
{
	FUNC_BEGIN();

	variant_t variant = this->datachannel->ordered();
	*val = variant;
	FUNC_END_RET_S(S_OK);
}
STDMETHODIMP DataChannel::get_maxPacketLifeTime(VARIANT* val)
{
	FUNC_BEGIN();

	variant_t variant = this->datachannel->maxRetransmitTime();
	*val = variant;
	FUNC_END_RET_S(S_OK);
}
STDMETHODIMP DataChannel::get_negotiated(VARIANT* val)
{
	FUNC_BEGIN();

	variant_t variant = this->datachannel->negotiated();
	*val = variant;
	FUNC_END_RET_S(S_OK);
}
STDMETHODIMP DataChannel::get_id(VARIANT* val)
{
	FUNC_BEGIN();

	variant_t variant = this->datachannel->id();
	*val = variant;
	FUNC_END_RET_S(S_OK);
}
STDMETHODIMP DataChannel::get_priority(VARIANT* val)
{
	FUNC_BEGIN();

	variant_t variant = this->priority.c_str();
	*val = variant;
	FUNC_END_RET_S(S_OK);
}

STDMETHODIMP DataChannel::get_readyState(VARIANT* val)
{
	FUNC_BEGIN();

	variant_t variant = webrtc::DataChannelInterface::DataStateString(this->datachannel->state());
	*val = variant;
	FUNC_END_RET_S(S_OK);
}
STDMETHODIMP DataChannel::get_bufferedAmount(VARIANT* val)
{
	variant_t variant = this->datachannel->buffered_amount();
	*val = variant;
	FUNC_END_RET_S(S_OK);
}
STDMETHODIMP DataChannel::get_bufferedAmountLowThreshold(VARIANT* val)
{
	FUNC_BEGIN();

	variant_t variant = this->bufferedAmountLowThreshold;
	*val = variant;
	FUNC_END_RET_S(S_OK);
}
STDMETHODIMP DataChannel::put_bufferedAmountLowThreshold(VARIANT val)
{
	FUNC_BEGIN();

	bufferedAmountLowThreshold = GetInt(&val, 0);
	FUNC_END_RET_S(S_OK);
}

// RTCPeerConnection event handlers
STDMETHODIMP DataChannel::put_onopen(VARIANT handler) { return MarshalCallback(onopen, handler); }
STDMETHODIMP DataChannel::put_onbufferedamountlow(VARIANT handler) { return MarshalCallback(onbufferedamountlow, handler); }
STDMETHODIMP DataChannel::put_onerror(VARIANT handler) { return MarshalCallback(onerror, handler); }
STDMETHODIMP DataChannel::put_onclose(VARIANT handler) { return MarshalCallback(onclose, handler); }
STDMETHODIMP DataChannel::put_onmessage(VARIANT handler)
{
	FUNC_BEGIN();

	HRESULT hr = MarshalCallback(onmessage, handler);
	//If ok
	if (!FAILED(hr))
	{
		//Deliver pending messages
		for (auto buffer : pending)
		{
			//Check buffer type
			if (!buffer.binary)
			{
				//Create string
				std::string message((const char*)buffer.data.data(), buffer.size());
				//Event
				DispatchAsync(onmessage, message);
			}
		}
		//Remove all pending messages
		pending.clear();
	}

	FUNC_END_RET_S(hr);
}
