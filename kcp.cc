#include "kcp.hh"
#include <cstdlib>
#include <malloc.h>
#include <cassert>
#include <algorithm>
#include <cstring>

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
	if(rcv_queue.size() < rcv_wnd)
		return rcv_wnd - rcv_queue.size();
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

// 上层定时调用，发送缓冲区数据以及ACK等信息
void KCPCB::update(U32 current) {

	this->current = current;

	if(updated == 0) {
		updated = 1;
		ts_flush = current;
	}

	// 当前时间是否到了刷新的时候
	I32 slap = u32diff(current, ts_flush);

	if(slap >= 10000 || slap < -10000) {
		ts_flush = current;
		slap = 0;
	}

	if(slap >= 0) {
		ts_flush += interval;
		if(u32diff(current, ts_flush) >= 0)
			ts_flush = current + interval;
		flush();
	}
}

// 从snd_buf中删除小于una的包, 并将snd_una更新
void KCPCB::parse_una(U32 una) {
	while(!snd_buf.empty()) {
		auto &q_front = snd_buf.front();
		if(u32diff(q_front.sn , una) < 0)
			snd_buf.pop_front();
		else
			break;
	}

	// 更新snd_una
	if(!snd_buf.empty())
		snd_una = snd_buf.front().sn;
	else
		snd_una = snd_nxt;
}

// 计算超时重传时间, 和计网自顶向下3.5.3节的公式一样
void KCPCB::update_rto(I32 rtt) {
	// rtt：该数据包的往返时间
	// rx_srtt: 平滑的rtt,近8次rtt平均值
	// rx_rttval: 近4次rtt和srtt的平均差值，反应了rtt偏离srtt的程度
	// rx_rto: 重传超时时间

	I32 rto = 0;
	if(rx_srtt == 0) {
		rx_srtt = rtt;
		rx_rttval = rtt / 2;
	} else {
		long delta = rtt - rx_srtt;
		if (delta < 0) delta = -delta;
		rx_rttval = (3 * rx_rttval + delta) / 4;
		rx_srtt = (7 * rx_srtt + rtt) / 8;
		if (rx_srtt < 1) rx_srtt = 1;
	}
	rto = rx_srtt + max(interval, 4 * (U32)rx_rttval); // rx_srtt + 4*rx_rttval表示一种比较坏的情况
	rx_rto = _ibound_(rx_minrto, rto, KCP_RTO_MAX);
}

// 把snd_buf里序号为sn的删掉, 对方已经收到了, 并将snd_una更新
void KCPCB::parse_ack(U32 sn) {
	if(u32diff(sn , snd_una) < 0 || u32diff(sn, snd_nxt) >= 0)
		return;
	
	for(auto it = snd_buf.begin(); it != snd_buf.end(); ++it) {
		if(it->sn == sn) {
			snd_buf.erase(it);
			break;
		}
		if(u32diff(sn, it->sn) < 0)
			break;
	}

	// 更新snd_una
	if(!snd_buf.empty())
		snd_una = snd_buf.front().sn;
	else
		snd_una = snd_nxt;
}

void KCPCB::ack_push(U32 sn, U32 ts) {
	acklist.push_back(sn);
	acklist.push_back(ts);
}

// 检查是否应该接收sn包, 接收后将rcv_buf中连续的包转移到rcv_queue供上层读取
void KCPCB::parse_data(U32 sn){
	bool repeat = false;

	// 新数据包在接收窗口之外, 不接收
	if(u32diff(sn, rcv_nxt+rcv_wnd) >=0 || u32diff(sn, rcv_nxt) < 0)
		return;
	
	// 是否接受过该包
	auto it = rcv_buf.begin();
	++it; // 第一个位置暂放放这个包, 所以跳过第一个
	for(; it != rcv_buf.end(); ++it) {
		if(sn == it->sn) {
			repeat = true;
			break;
		} else if(u32diff(it->sn, sn) > 0) {
			break;
		}
	}

	if(!repeat) {
		rcv_buf.splice(it, rcv_buf, rcv_buf.begin());
	}else{ // 这个包丢掉
		rcv_buf.pop_front();
	}

	// 将收到的连续数据包从rcv_buf 转移到 rcv_queue 供上层应用读取
	while(!rcv_buf.empty()) {
		auto it = rcv_buf.begin();
		if(it->sn == rcv_nxt && rcv_queue.size() < rcv_wnd) {//数据包连续 且 rcv_queue 不满
			rcv_queue.splice(rcv_queue.end(), rcv_buf, it);
			++rcv_nxt;
		}else{
			break;
		}
	}
}


// 计算快速重传的参数fastack(发送缓存里的包被跳过的总次数,冗余ACK)
void KCPCB::parse_fastack(U32 maxack, U32 ts) {
	if(u32diff(maxack, snd_una) < 0 || u32diff(maxack, snd_nxt) >= 0)
		return;
	
	// 统计maxack之前的有几个没有被ack包
	for(auto &seg: snd_buf) {
		if(u32diff(maxack, seg.sn) < 0)
			break;
		if(maxack != seg.sn) {
			#ifndef IKCP_FASTACK_CONSERVE
				++seg.fastack; // 该数据包被跳过一次
			#else
				if (u32diff(ts, seg.ts) >= 0)
					++seg.fastack;
			#endif
		}
	}
}


// 从下层协议接收数据, data中可能好几个kcp包
int KCPCB::input(const char *data, long sz) {
	bool flag = false;
	U32 maxack, latest_ts, prev_una = snd_una;
	while(1) { // 循环读取报文
		U32 ts, sn, len, una, conv;
		U16 wnd;
		U8 cmd, frg;

		if(sz < (int)KCP_OVERHEAD) break;

		data = kcp_decode32u(data, &conv);
		if(conv != this->conv) return -1;

		// 取得所有包头信息
		data = kcp_decode8u(data, &cmd);
		data = kcp_decode8u(data, &frg);
		data = kcp_decode16u(data, &wnd);
		data = kcp_decode32u(data, &ts);
		data = kcp_decode32u(data, &sn);
		data = kcp_decode32u(data, &una);
		data = kcp_decode32u(data, &len);

		sz -= (int)KCP_OVERHEAD;

		if(sz < (long)len || (int)len < 0) return -2;

		if(	cmd != KCP_CMD_PUSH && cmd != KCP_CMD_ACK &&
			cmd != KCP_CMD_WASK && cmd != KCP_CMD_WINS)
			return -3;

		// wnd: 对方的接收窗口
		rmt_wnd = wnd;
		// una: 期待数据包, una之前的都收到了可以删除
		parse_una(una);

		if(cmd == KCP_CMD_ACK) {

			// ts为该数据包发送时间, current为当前时间(收到该数据包的ack), 两者之差为rtt
			if(u32diff(current, ts) >= 0) {
				update_rto(current - ts);
			}

			/*	
				parse_ack() 把snd_buf里序号为sn的删掉, 对方已经收到了
				维护maxack, 小于maxack的一定没有收到ack,
				已经收到ack的已经删除, ikcp_parse_fastack()不会遍历到
			*/
			parse_ack(sn);
			
			// 更新maxack和latest_ts
			if(!flag) {
				flag = true;
				maxack = sn, latest_ts = ts;
			} else if(u32diff(sn , maxack) > 0) {
				#ifndef IKCP_FASTACK_CONSERVE
					maxack = sn;
					latest_ts = ts;
				#else
					if (u32diff(ts, latest_ts) > 0) {
						maxack = sn;
						latest_ts = ts;
					}
				#endif
			}
		} else if(cmd == KCP_CMD_WASK) {	// 发送方探测接收方的窗口大小
			probe |= KCP_ASK_TELL;
		} else if(cmd == KCP_CMD_WINS) {
			// do nothing
		} else if(cmd == KCP_CMD_PUSH) {
			if(u32diff(sn, rcv_nxt+rcv_wnd) < 0) {// 判断是否超出(大于)接收窗口
				ack_push(sn, ts);
				if(u32diff(sn, rcv_nxt) >= 0) { // 判断是否超出(小于)接收窗口
					// 先将数据放在第一个位置, 在parse_data里将其放在正确位置
					rcv_buf.emplace_front();
					auto &q_front = rcv_buf.front();

					q_front.conv = conv;
					q_front.cmd = cmd;
					q_front.frg = frg;
					q_front.wnd = wnd;
					q_front.ts = ts;
					q_front.sn = sn;
					q_front.una = una;
					q_front.len = len;

					if(len > 0 && q_front.add_data(data, len) == len)
						parse_data(sn);
					else
						rcv_buf.pop_front();
				}
			}
		}else{
			return -3;
		}

		data += len;
		sz -= len;
	}

	if(flag) {
		parse_fastack(maxack, latest_ts);
	}

	/*  
		接收时，拥塞窗口增加
		拥塞窗口控制, 慢启动：每收到一个ACK，拥塞窗口就加1，一个RTT拥塞窗口会翻倍增长。 
		拥塞避免：每收到一个ACK，拥塞窗口就加1/cwnd, 一个RTT拥塞窗口加1
		下边发送时，拥塞窗口减少
	*/
	if (u32diff(snd_una, prev_una) > 0) {
		if (cwnd < rmt_wnd) {
			U32 mss = mss;
			// 拥塞窗口小于慢启动阈值，快速增加拥塞窗口
			if (cwnd < ssthresh) { 
				cwnd++; // 慢启动
				incr += mss;
				// 达到阈值的时候 cwnd*mss = incr
			}	
			// 拥塞窗口大于慢启动阈值，拥塞避免模式, 慢速增加拥塞窗口
			else { 	
				if (incr < mss) incr = mss;
				// 经过计算，差不多是每个RTT增加一个mss, 间隔略小于一个RTT，也就是更快地增加拥塞窗口
				// incr = k*mss , 拥塞窗口等于floor(k)
				incr += (mss * mss) / incr + (mss / 16); 
				if ((cwnd + 1) * mss <= incr) {
				#if 1
					cwnd = (incr + mss - 1) / ((mss > 0)? mss : 1);
				#else
					cwnd++;
				#endif
				}
			}
			if (cwnd > rmt_wnd) {
				cwnd = rmt_wnd;
				incr = rmt_wnd * mss;
			}
		}
	}

	return 0;
}	

