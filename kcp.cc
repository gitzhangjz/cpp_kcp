#include "kcp.hh"
#include <cstdlib>
#include <malloc.h>
#include <cassert>
#include <algorithm>
#include <cstring>

using std::min, std::max;

/*
	CMD_ACK:
		对报文的ack, 需要：ts, sn
*/

KCPSEG::KCPSEG(I32 data_sz) {
	len = 0, capacity = data_sz, xmit = 0;
	if(data_sz > 0)
		offset_p = data = new char[data_sz];
	else
		offset_p = data = nullptr;
}

KCPSEG::~KCPSEG() {
	delete []data;
}

// len >= 0：充入len长度的数据, 如果容量不够, 则充满为止
// 返回充入数据的长度
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

	// nrcv_buf = 0;
	// nsnd_buf = 0;
	// nrcv_que = 0;
	// nsnd_que = 0;
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

int KCPCB::kcp_output(const char *buf, int len) {
	assert(output);

	if(len == 0) return 0;
	return output(buf, len, *this, user);
}

// 拿到第i个ack的sn和ts
void KCPCB::ack_get(int i, U32& sn, U32& ts) {
	sn = acklist[i*2];
	ts = acklist[i*2 + 1];
}


int KCPCB::rcvwnd_unused() {
	if(nrcv_que < rcv_wnd)
		return rcv_wnd - nrcv_que;
	return 0;
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

// 发送数据及ACK等信息
void KCPCB::flush() {
	if(updated == 0) return;

	char *ptr = buffer;

	// 发送缓存空间不够的时候清空缓存。保证buffer能装下input_sz
	auto try_put_buffer = [&ptr, this](int input_sz) {
		int sz = (int)(ptr - buffer); // 缓存中的数据大小

		// buffer 塞不下, 先把数据发送掉
		if(sz + input_sz > (int)KCP_MTU_DEF) {
			kcp_output(buffer, sz);
			ptr = buffer;
		}
	};

	U32 current = current;

	KCPSEG seg(0);

	seg.conv = conv;
	seg.cmd = KCP_CMD_ACK;
	seg.frg = 0;
	seg.wnd = rcvwnd_unused();
	seg.una = rcv_nxt;
	seg.len = seg.sn = seg.ts = 0;

	// Step1. 收到报文, 回复ack, 没有报文, 只发包头
	// 包头需要: CMD_ACK, ts, sn
	int count = ackcount, i;

	for(i = 0; i < count; ++i) {
		try_put_buffer((int)KCP_OVERHEAD);
		ack_get(i, seg.sn, seg.ts);
		ptr = kcp_encode_seg(ptr, seg);
	}
	ackcount = 0;

	// Step2. 检测对面窗口, 发现对面窗口满了, 探测窗口, 没有报文, 只发包头
	// 包头需要: IKCP_CMD_WASK
	if(rmt_wnd == 0) {
		// 第一次探测, 设置探测窗口时间间隔
		if(probe_wait == 0) {
			probe_wait = KCP_PROBE_INIT;
			ts_probe = current + probe_wait;
		} else if(u32diff(current , ts_probe) > 0) {
			// 达到下次探测的时间戳依然没有窗口, 探测一次窗口, 然后延长探测窗口时间
			if(probe_wait < KCP_PROBE_INIT) // 防止回绕
				probe_wait = KCP_PROBE_INIT;
			probe_wait += probe_wait / 2;
			if(probe_wait > KCP_PROBE_LIMIT)
				probe_wait = KCP_PROBE_LIMIT;
			ts_probe = current + probe_wait;
			probe |= KCP_ASK_SEND; // 标记窗口
		}
	}else { // 发现窗口没满, 重置探测时间
		ts_probe = 0;
		probe_wait = 0;
	}

	if(probe & KCP_ASK_SEND) {
		seg.cmd = KCP_CMD_WASK;
		try_put_buffer((int)KCP_OVERHEAD);
		ptr = kcp_encode_seg(ptr, seg);
	}

	// Step3. 回答窗口大小
	if(probe & KCP_ASK_TELL) {
		seg.cmd = KCP_CMD_WINS;
		try_put_buffer((int)KCP_OVERHEAD);
		ptr = kcp_encode_seg(ptr, seg);
	}

	// 窗口探测相关信息发送完毕
	probe = 0;

	// Step4. 发送上层数据, 从snd_queue中取出数据包放到snd_buf中, 直到snd_buf满或者snd_queue为空
	 
	U32 wnd = min(snd_wnd, rmt_wnd);
	if(nocwnd == 0) // 有拥塞控制
		wnd = min(wnd, cwnd);
	
	while(u32diff(snd_nxt, snd_una + wnd) < 0 && !snd_queue.empty()) {
		// 将snd_queue队尾放在snd_buf队头
		snd_buf.splice(snd_buf.end(), snd_queue, snd_queue.begin());
		auto &q_back = snd_buf.back();

		q_back.conv = conv;
		q_back.cmd = KCP_CMD_PUSH;
		q_back.wnd = seg.wnd;
		q_back.ts = current;
		q_back.sn = snd_nxt++;
		q_back.una = rcv_nxt;
		q_back.resendts = current;
		q_back.rto = rx_rto;
		q_back.fastack = 0;
		q_back.xmit = 0;
	}

	
	U32 resent = (fastresend > 0) ? (U32)fastresend : 0xffffffff,
		rtomin = (nodelay_flag == 0) ? (rx_rto >> 3) : 0;
	bool lost = false, change = false;
	// 遍历snd_buf里的数据包是否需要发送
	// 该数据包是否需要发送，三种情况：
	// 	  1. 该数据包没发送过
	// 	  2. 超时没有收到ack，触发超时重传
	// 	  3. 收到resent次冗余ack，触发快速重传
	for(auto &snd_seg : snd_buf) {
		bool needsend = false;
		
		// 1. 第一次发送
		if(snd_seg.xmit == 0) {
			needsend = true;
			snd_seg.xmit = 1;
			snd_seg.rto = rx_rto;
			snd_seg.resendts = current + snd_seg.rto + rtomin;
		}
		// 2.超时重传
		else if(u32diff(current , snd_seg.resendts) >= 0) {
			needsend = true;
			++snd_seg.xmit;
			++xmit;
			if(nodelay_flag == 0) { // TCP模式，超时时间*2
				snd_seg.rto += max(snd_seg.rto, rx_rto);
			} else {	// 快速模式
				I32 step = (nodelay_flag < 2) ? ((I32)snd_seg.rto) : rx_rto;
				snd_seg.rto += step / 2;
			}
			snd_seg.resendts = current + snd_seg.rto;
			lost = true;
		}
		// 3. 快速重传
		else if(snd_seg.fastack >= resent) {
			if((int)snd_seg.xmit <= fastlimit || fastlimit <=0 ) {
				needsend = true;
				++snd_seg.xmit;
				snd_seg.fastack = 0;
				snd_seg.resendts = current + snd_seg.rto;
				change = true;
			}
		}

		if(needsend) {
			snd_seg.ts = current;
			snd_seg.wnd = seg.wnd;
			snd_seg.una = rcv_nxt;

			int input_sz = KCP_OVERHEAD + snd_seg.len;
			try_put_buffer(input_sz);

			if(snd_seg.len > 0) {
				memcpy(ptr, snd_seg.data, snd_seg.len);
				ptr += snd_seg.len;
			}

			// 这里用数据包的重传次数
			if(snd_seg.xmit >= dead_link)
				state = (U32)-1;
		}
	}

	//把缓存里的数据发出去
	try_put_buffer((int)KCP_MTU_DEF+1);

	// 如果触发了快速重传，减小拥塞窗口（快速恢复）
	if(change) {
		U32 inflight = snd_nxt - snd_una;
		ssthresh = inflight / 2;

		if(ssthresh < KCP_THRESH_MIN)
			ssthresh = KCP_THRESH_MIN;
		cwnd = ssthresh + resent;
		incr = cwnd * mss;
	}

	// 丢包了，减小拥塞窗口
	if(lost) {
		ssthresh = cwnd / 2;
		if(ssthresh < KCP_THRESH_MIN)
			ssthresh = KCP_THRESH_MIN;
		cwnd = 1;
		incr = mss;
	}

	if(cwnd < 1) {
		cwnd = 1;
		incr = mss;
	}
}