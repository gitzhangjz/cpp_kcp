#ifndef KCP_HH
#define KCP_HH

#include <list>
#include <vector>

using std::list, std::vector, std::min, std::max;

//=====================================================================
// 32BIT INTEGER DEFINITION 
//=====================================================================
#ifndef __INTEGER_32_BITS__
#define __INTEGER_32_BITS__
#if defined(_WIN64) || defined(WIN64) || defined(__amd64__) || \
	defined(__x86_64) || defined(__x86_64__) || defined(_M_IA64) || \
	defined(_M_AMD64)
	typedef unsigned int ISTDUINT32;
	typedef int ISTDINT32;
#elif defined(_WIN32) || defined(WIN32) || defined(__i386__) || \
	defined(__i386) || defined(_M_X86)
	typedef unsigned long ISTDUINT32;
	typedef long ISTDINT32;
#elif defined(__MACOS__)
	typedef UInt32 ISTDUINT32;
	typedef SInt32 ISTDINT32;
#elif defined(__APPLE__) && defined(__MACH__)
	#include <sys/types.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif defined(__BEOS__)
	#include <sys/inttypes.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif (defined(_MSC_VER) || defined(__BORLANDC__)) && (!defined(__MSDOS__))
	typedef unsigned __int32 ISTDUINT32;
	typedef __int32 ISTDINT32;
#elif defined(__GNUC__)
	#include <stdint.h>
	typedef uint32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#else 
	typedef unsigned long ISTDUINT32; 
	typedef long ISTDINT32;
#endif
#endif


//=====================================================================
// Integer Definition
//=====================================================================
#ifndef __IINT8_DEFINED
#define __IINT8_DEFINED
typedef char I8;
#endif

#ifndef __IUINT8_DEFINED
#define __IUINT8_DEFINED
typedef unsigned char U8;
#endif

#ifndef __IUINT16_DEFINED
#define __IUINT16_DEFINED
typedef unsigned short U16;
#endif

#ifndef __IINT16_DEFINED
#define __IINT16_DEFINED
typedef short I16;
#endif

#ifndef __IINT32_DEFINED
#define __IINT32_DEFINED
typedef ISTDINT32 I32;
#endif

#ifndef __IUINT32_DEFINED
#define __IUINT32_DEFINED
typedef ISTDUINT32 U32;
#endif

#ifndef __IINT64_DEFINED
#define __IINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef __int64 IINT64;
#else
typedef long long I64;
#endif
#endif

#ifndef __IUINT64_DEFINED
#define __IUINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef unsigned __int64 IUINT64;
#else
typedef unsigned long long U64;
#endif
#endif
//---------------------------------------------------------------------
// BYTE ORDER & ALIGNMENT
//---------------------------------------------------------------------
#ifndef IWORDS_BIG_ENDIAN
    #ifdef _BIG_ENDIAN_
        #if _BIG_ENDIAN_
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #if defined(__hppa__) || \
            defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
            (defined(__MIPS__) && defined(__MIPSEB__)) || \
            defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
            defined(__sparc__) || defined(__powerpc__) || \
            defined(__mc68000__) || defined(__s390x__) || defined(__s390__)
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
		// 大端字节序
        #define IWORDS_BIG_ENDIAN  0
    #endif
#endif

#ifndef IWORDS_MUST_ALIGN
	#if defined(__i386__) || defined(__i386) || defined(_i386_)
		#define IWORDS_MUST_ALIGN 0
	#elif defined(_M_IX86) || defined(_X86_) || defined(__x86_64__)
		#define IWORDS_MUST_ALIGN 0
	#elif defined(__amd64) || defined(__amd64__)
		#define IWORDS_MUST_ALIGN 0
	#else
		#define IWORDS_MUST_ALIGN 1
	#endif
#endif

//=====================================================================
// KCP BASIC
//=====================================================================
constexpr U32 KCP_RTO_NDL = 30;		// no delay min rto
constexpr U32 KCP_RTO_MIN = 100;		// normal min rto
constexpr U32 KCP_RTO_DEF = 200;
constexpr U32 KCP_RTO_MAX = 60000;

constexpr U32 KCP_CMD_PUSH = 81;		// cmd: 发送数据
constexpr U32 KCP_CMD_ACK  = 82;		// cmd: 收到数据（ack）
constexpr U32 KCP_CMD_WASK = 83;		// cmd: 探测窗口 (ask)
constexpr U32 KCP_CMD_WINS = 84;		// cmd: 回答窗口 (tell)

constexpr U32 KCP_ASK_SEND = 1;		// 标记: 探测窗口
constexpr U32 KCP_ASK_TELL = 2;		// 标记：回答窗口

constexpr U32 KCP_WND_SND = 32;
constexpr U32 KCP_WND_RCV = 128;       // must >= max fragment size
constexpr U32 KCP_MTU_DEF = 1400;		// mtu
constexpr U32 KCP_ACK_FAST	= 3;
constexpr U32 KCP_INTERVAL	= 100;
constexpr U32 KCP_OVERHEAD = 24;		// kcp 包头大小
constexpr U32 KCP_DEADLINK = 20;
constexpr U32 KCP_THRESH_INIT = 2;
constexpr U32 KCP_THRESH_MIN = 2;
constexpr U32 KCP_PROBE_INIT = 7000;		// 7 secs to probe window size
constexpr U32 KCP_PROBE_LIMIT = 120000;	// up to 120 secs to probe window
constexpr U32 KCP_FASTACK_LIMIT = 5;		// max times to trigger fastack


class KCPSEG {
	KCPSEG& operator=(KCPSEG&) {} 
	KCPSEG(const KCPSEG&) {}
public:
	I32 conv;	// 会话ID
	I32 cmd;	// 命令
	I32 frg;	// 上层传下来的数据一个KCP包装不完需要分段（段号是倒序的也就是段3、2、1、0组装出上层数据，段号一定以0结尾）。（如果是字节流模式就没有分段）
	I32 wnd;	// 窗口大小

	// 时间戳，如果是数据包，每次重传会更新ts
	// PUSH: 数据包的发出时间。 
	// ACK：数据包的发出时间而不是ACK发出的时间，和收到的PUSH的ts一样， 发送方收到ACK用这个ts方便计算rtt
	U32 ts;		
	
	// 	sn
	//	CMD_ACK:发送方告诉接收方: 我在ack你的第几个包
	//	ACM_PUSH:发送方发送的第几个包	
	U32 sn;			
	// 未确认序列号, 发送方告诉接收方：
	//	你发送的una之前的包都已经收到了, 期待una号包
	I32 una;		
	I32 len;  		// 数据包除去头部的字节数

	/*---------------以上为overhead，以下为辅助信息--------------*/

	I32 capacity; 	// 数据总容量
	I32 resendts; 	// = current + rto， 超时重传的阈值, 当前时间超过resendts, 就要重发这个数据包

	// Retransmission Timeout, 下次超时重传的间隔时间
	// 会随着超时次数增加, 增加速率取决于是不是快速模式
	I32 rto;		
	I32 fastack;	// 数据包被跳过次数, 快速重传功能需要
	I32 xmit;  		// 该数据包发送次数, transmit 的缩写, , 次数太多判断网络断开

	char *data, *offset_p;

	KCPSEG(I32 data_sz = KCP_MTU_DEF-KCP_OVERHEAD);

	// len >= 0：充入len长度的数据, 如果容量不够, 则充满为止
	// 返回充入数据的长度
	int add_data(const char*buffer, int len);

	~KCPSEG();
};

class KCPCB {
	KCPCB& operator=(const KCPCB&) {}
public:
	U32 conv; 		// 会话ID
	U32 mtu; 		// 下层协议的最大传输单元, 一次发送若干个kcp包, 这些包的总长度不超过mtu
	U32 mss; 		// 一个KCP数据包的最大数据载荷, mss+head一定不超过mtu, 默认下层数据面向数据报
	U32 state; 		// 连接状态

	U32 snd_una; 	// 我发的snd_una之前的包都已经收到了
	U32 snd_nxt;  	// 下一个要发送的包（从send_que发到send_buf）的序列号
	U32 rcv_nxt;	// 下一个要接收的包(从rcv_buf发到rcv_que)的序列号

	U32 ts_recent; 	// 没用到
	U32 ts_lastack;	// 没用到

	U32 ssthresh;	// 拥塞窗口从慢启动到拥塞避免的阈值
	I32 rx_rttval;	// 近4次rtt和srtt的平均差值，反应了rtt偏离srtt的程度
	I32 rx_srtt;	// 平滑的rtt,近8次rtt平均值
	I32 rx_rto;		//系统的重传超时时间
	I32 rx_minrto; 	// 最小重传超时时间
	
	U32 snd_wnd; 	// 发送窗口大小
	U32 rcv_wnd; 	// 接收窗口大小
	U32 rmt_wnd; 	// 对方接收窗口大小
	U32 cwnd; 		// 拥塞窗口大小
	U32 probe;		// 探测窗口的相关标记 IKCP_ASK_SEND 和 IKCP_ASK_TELL

	U32 current;	// 当前时间戳
	U32 interval; 	// 内部flush刷新间隔
	U32 ts_flush; 	// 下一次刷新输出的时间戳
	U32 xmit;		// 该KCP连接超时重传次数

	// U32 nrcv_buf; 	// rcv_buf的长度
	// U32 nsnd_buf;	// snd_buf的长度
	// U32 nrcv_que; 	// rcv_que的长度
	// U32 nsnd_que; 	// snd_que的长度

	U32 nodelay_flag;	// 是否启用nodelay模式
	U32 updated;	// 是否调用过update函数
	U32 ts_probe; 	// 下次探测窗口大小的时间戳
	U32 probe_wait; // 探测窗口大小的间隔时间，每次探测对面窗口为0（失败）, 探测时间*1.5
	U32 dead_link;	// 断开连接的重传次数阈值
	U32 incr; 		// k*mss , 拥塞窗口等于floor(k)
	list<KCPSEG> snd_queue;
	list<KCPSEG> rcv_queue;
	list<KCPSEG> snd_buf; // 一个node指针, 循环链表里的某个节点
	list<KCPSEG> rcv_buf; // 接收缓存, 将收到的数据暂存, 然后将其中连续的数据放到rcv_queue供上层读取
	vector<U32> acklist; // 一个整数数组，存放要回答的ack，结构为 [sn0, ts0, sn1, ts1, ...]
	U32 ackcount; // 本次需要回复的ack个数
	U32 ackblock;
	void *user;
	char *buffer; // 发送数据的缓冲区
	int fastresend; // 快速重传的失序阈值, 发送方收到 fastresend 个冗余ACK就触发快速重传
	int fastlimit;  // 快速重传的次数限制
	int nocwnd;		// 0: 有拥塞控制, 1: 没有拥塞控制
	int stream;		// 流模式
	int logmask;
	int (*output)(const char *buf, int len, const KCPCB& kcp, void *user);
	void (*writelog)(const char *log, const KCPCB& kcp, void *user);

	KCPCB(U32 conv, void *user);

	~KCPCB();

	void ack_get(int i, U32& sn, U32& ts);

	void set_output(int (*output)(const char *buf, int len, 
	const KCPCB& kcp, void *user));

	int recv(char *buffer, int len);

	int send(const char *buffer, int len);

	void update(U32 current);

	U32 check(U32 current);

	void parse_una(U32 una);

	void update_rto(I32 rtt);

	void parse_ack(U32 sn);

	void ack_push(U32 sn, U32 ts);

	void parse_data(U32 sn);

	void parse_fastack(U32 sn, U32 ts);

	int input(const char *data, long len);

	void flush();

	int kcp_output(const char *buf, int len);

	int peeksize();

	int setmtu(int mtu);

	int rcvwnd_unused();
	
	int set_wndsize(int sndwnd, int rcvwnd);

	int waitsnd();

	int nodelay(int nodelay, int interval, int resend, int nc);

	void ikcp_allocator(void* (*new_malloc)(size_t), void (*new_free)(void*));

	U32 ikcp_getconv(const void *ptr);
	
};

inline U32 _ibound_(U32 lower, U32 middle, U32 upper) 
{
	return min(max(lower, middle), upper);
}

// 将dw放在p位置返回p+4
inline char *kcp_encode32u(char *p, U32 dw) {
	#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
		*(unsigned char*)(p + 0) = (unsigned char)((dw >>  0) & 0xff);
		*(unsigned char*)(p + 1) = (unsigned char)((dw >>  8) & 0xff);
		*(unsigned char*)(p + 2) = (unsigned char)((dw >> 16) & 0xff);
		*(unsigned char*)(p + 3) = (unsigned char)((dw >> 24) & 0xff);
	#else
		memcpy(p, &dw, 4);
	#endif
	p += 4;
	return p;
}

// 将p位置的数据放在dw, 返回p + 4
inline const char *kcp_decode32u(const char *p, U32 *dw)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*l = *(const unsigned char*)(p + 3);
	*l = *(const unsigned char*)(p + 2) + (*dw << 8);
	*l = *(const unsigned char*)(p + 1) + (*dw << 8);
	*l = *(const unsigned char*)(p + 0) + (*dw << 8);
#else 
	memcpy(dw, p, 4);
#endif
	// p + 4字节
	p += 4;
	return p;
}

// 将w放在p位置返回p+2
inline char *kcp_encode16u(char *p, U16 w) {
	#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
		*(unsigned char*)(p + 0) = (w & 255);
		*(unsigned char*)(p + 1) = (w >> 8);
	#else
		memcpy(p, &w, 2);
	#endif
	p += 2;
	return p;
}

// 将p位置的数据放在w, 返回p + 2
inline const char *kcp_decode16u(const char *p, U16 *w)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*w = *(const unsigned char*)(p + 1);
	*w = *(const unsigned char*)(p + 0) + (*w << 8);
#else
	memcpy(w, p, 2);
#endif
	p += 2;
	return p;
}

// 将c放在p位置返回p+1
inline char *kcp_encode8u(char *p, U8 c) {
	*(unsigned char*)p++ = c;
	return p;
}

// 将p位置的数据放在c, 返回p + 1
inline const char *kcp_decode8u(const char *p, unsigned char *c)
{
	*c = *(unsigned char*)p++;
	return p;
}

// 将数据包头部编码进ptr指向的buffer中，返回编码后ptr指向的位置
inline char *kcp_encode_seg(char *ptr, const KCPSEG &seg) {
	ptr = kcp_encode32u(ptr, seg.conv);
	ptr = kcp_encode8u(ptr, (U8)seg.cmd);
	ptr = kcp_encode8u(ptr, (U8)seg.frg);
	ptr = kcp_encode16u(ptr, (U16)seg.wnd);
	ptr = kcp_encode32u(ptr, seg.ts);
	ptr = kcp_encode32u(ptr, seg.sn);
	ptr = kcp_encode32u(ptr, seg.una);
	ptr = kcp_encode32u(ptr, seg.len);
	return ptr;
}

inline I32 u32diff(U32 later, U32 earlier) 
{
	return ((I32)(later - earlier));
}
#endif