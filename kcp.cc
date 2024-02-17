#include "kcp.hh"
#include <cstdlib>
#include <malloc.h>
#include <cassert>
#include <algorithm>
#include <cstring>

using std::min;

KCPSEG::KCPSEG(I32 data_sz) {
	len = 0, capacity = data_sz, xmit = 0;
	offset_p = data = new char[data_sz];
}

KCPSEG::~KCPSEG() {
	delete []data;
}

int KCPSEG::add_data(const char*buffer, int len)  {
	int extend = min(len, capacity - this->len);
	
	if(!buffer || extend <= 0 || extend > len || extend > capacity-this->len) {
		return 0;
	}

	memcpy(offset_p, buffer, extend);
	offset_p += extend;
	this->len += extend;
	return extend;
}


KCPCB::KCPCB(U32 conv, void *user) 
{
	this->conv = conv;
	this->user = user;
	snd_una = 0;
	snd_nxt = 0;
	rcv_nxt = 0;
	ts_recent = 0;
	ts_lastack = 0;
	ts_probe = 0;
	probe_wait = 0;
	snd_wnd = KCP_WND_SND;
	rcv_wnd = KCP_WND_RCV;
	rmt_wnd = KCP_WND_RCV;
	cwnd = 0;
	incr = 0;
	probe = 0;
	mtu = KCP_MTU_DEF; 
	mss = mtu - KCP_OVERHEAD; 
	stream = 0;

	buffer = new char[(mtu + KCP_OVERHEAD) * 3];
	// 异常检查

	nrcv_buf = 0;
	nsnd_buf = 0;
	nrcv_que = 0;
	nsnd_que = 0;
	state = 0;
	ackblock = 0;
	ackcount = 0;
	rx_srtt = 0;
	rx_rttval = 0;
	rx_rto = KCP_RTO_DEF;
	rx_minrto = KCP_RTO_MIN;
	current = 0;
	interval = KCP_INTERVAL;
	ts_flush = KCP_INTERVAL;
	nodelay_flag = 0;
	updated = 0;
	logmask = 0;
	ssthresh = KCP_THRESH_INIT;
	fastresend = 0;
	fastlimit = KCP_FASTACK_LIMIT;
	nocwnd = 0;
	xmit = 0;
	dead_link = KCP_DEADLINK;
	output = NULL;
	writelog = NULL;
}

KCPCB::~KCPCB() {
	delete []buffer;
}

void KCPCB::set_output(int (*output)(const char *buf, int len,
	const KCPCB& kcp, void *user)) 
{
	this->output = output;
}

// 将上层数据包buffer放入snd_queue发送队列
int KCPCB::send(const char *buffer, int len) 
{
	assert(mss > 0);
	if(len < 0) return -1;

	// 在流模式下, 将队尾数据包充满, 有粘包现象。
	if(stream && !snd_queue.empty()) {
		auto &q_back = snd_queue.back();
		I32 extend = q_back.add_data(buffer, len);
		buffer += extend;
		len -= extend;
		q_back.frg = 0;
		if(len <= 0) {
			return 0;
		}
	}

	// 计算需要几个包来放buffer
	int count = (len + mss - 1) / mss;
	if(count == 0) count = 1; // 空包(len = 0)的情况

	if(count >= (int)KCP_WND_RCV) return -2;

	for(int i = 0; i < count; ++i) {
		snd_queue.emplace_back();
		auto &q_back = snd_queue.back();
		int extend = q_back.add_data(buffer, len);
		if(buffer) {
			buffer += extend;
		}
		len -= extend;
	}

	return 0;
}