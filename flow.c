//---------------------------------------------------------------------------------------------
//
// Copyright (c) 2018, fmad engineering llc 
//
// The MIT License (MIT) see LICENSE file for details 
// 
// guts of the flow calcuation. generats a SHA1 of the MAC/IP/Proto/Port
// and maps packets into this
//
// on the interop timecompressed(x100) + (192B sliced) data following performance is seen
//
// interop_scaled_20181204_2054     15GB Chunk(Cnt:   63842 Start: 4100250 End: 4164091 Comp:1.49) Inv:-nan Cap:-nan CacheI:-nan Cache:-nan Disk:-nan Drop:-nan Pkt:0
//
//
// 2018/12/07:   ~ 17Gbps ingress @ 43K flows per snapshot
//
// [11:54:56.639.887.104] Input:29.964 GB  17.00 Gbps PCAP: 248.85 Gbps | Output 0.27410 GB Flows/Snap:  42113 FlowCPU:0.617 | ESPush:       0  42.64K ESErr    0 | OutputCPU: 0.000
// [11:54:56.705.689.088] Input:31.911 GB  16.73 Gbps PCAP: 254.24 Gbps | Output 0.27439 GB Flows/Snap:  43165 FlowCPU:0.617 | ESPush:       0   0.53K ESErr    0 | OutputCPU: 0.000
// [11:54:56.768.221.696] Input:33.900 GB  17.09 Gbps PCAP: 273.26 Gbps | Output 0.29819 GB Flows/Snap:  43591 FlowCPU:0.619 | ESPush:       0  43.59K ESErr    0 | OutputCPU: 0.000
// [11:54:56.831.990.784] Input:35.886 GB  17.05 Gbps PCAP: 267.44 Gbps | Output 0.31004 GB Flows/Snap:  43591 FlowCPU:0.616 | ESPush:       0  22.04K ESErr    0 | OutputCPU: 0.000
//
// 2018/12/27
// 
// PCAP interface using packet blocks
//
// [11:53:14.433.064.192] In:55.479 GB 2.52 Mpps 22.98 Gbps PCAP: 251.61 Gbps | Out 0.46024 GB Flows/Snap:  42101 FlowCPU:0.31 | ESPush:     0  42.09K ESErr    0 | OutCPU: 0.00 (0.00)
// [11:53:14.524.824.832] In:58.166 GB 2.53 Mpps 23.08 Gbps PCAP: 251.52 Gbps | Out 0.48452 GB Flows/Snap:  44751 FlowCPU:0.32 | ESPush:     0  44.75K ESErr    0 | OutCPU: 0.00 (0.00)
// [11:53:14.611.949.056] In:60.846 GB 2.55 Mpps 23.02 Gbps PCAP: 264.30 Gbps | Out 0.50738 GB Flows/Snap:  41926 FlowCPU:0.32 | ESPush:     0  41.92K ESErr    0 | OutCPU: 0.00 (0.00)
//
// PCAPWall time: 3.09 sec ProcessTime 37.40 sec (12.105)
//
// 2018/12/28
//
// FMAD chunked format + per CPU FlowIndex with Merged output 
//
// [00:26:29.381.616.896] In:42.241 GB 6.75 Mpps 61.25 Gbps PCAP: 254.51 Gbps | Out 0.36618 GB Flows/Snap:  40431 FlowCPU:0.87 | ESPush:     0  97.81K ESErr    0 | OutCPU: 0.00 (0.00)
// [00:26:29.620.605.696] In:49.352 GB 6.73 Mpps 61.08 Gbps PCAP: 255.59 Gbps | Out 0.42603 GB Flows/Snap:  55289 FlowCPU:0.87 | ESPush:     0 109.22K ESErr    0 | OutCPU: 0.00 (0.00)
// [00:26:29.864.099.840] In:56.482 GB 6.70 Mpps 61.25 Gbps PCAP: 251.53 Gbps | Out 0.47893 GB Flows/Snap:  42116 FlowCPU:0.87 | ESPush:     0  96.60K ESErr    0 | OutCPU: 0.00 (0.00)
// [00:26:30.089.333.248] In:63.266 GB 6.45 Mpps 58.26 Gbps PCAP: 258.71 Gbps | Out 0.56736 GB Flows/Snap:  44352 FlowCPU:0.88 | ESPush:     0 161.32K ESErr    0 | OutCPU: 0.00 (0.00)
//
// PCAPWall time: 16900787200.00 sec ProcessTime 17.74 sec (0.000)
// Total Time: 17.84 sec RawInput[44.211 Gbps 38906940 Pps] Output[0.469 Gbps] TotalLine:1909656 107021 Line/Sec
//
// 2019/01/07
//
// added 4 more CPUs (everything HWT1 Socket1) with ES Output code on Socket0
//
// [11:23:19.631.513.088] In:34.963 GB 6.63 Mpps 60.00 Gbps PCAP: 260.45 Gbps | Out 0.29738 GB Flows/Snap:  43298 FlowCPU:0.42 | ESPush:     0 105.85K ESErr    0 | OutCPU: 0.00 (0.00)
// [11:23:19.867.406.080] In:41.963 GB 6.63 Mpps 60.12 Gbps PCAP: 254.90 Gbps | Out 0.35608 GB Flows/Snap:  40994 FlowCPU:0.42 | ESPush:     0 107.84K ESErr    0 | OutCPU: 0.00 (0.00)
// [11:23:20.101.664.768] In:48.934 GB 6.60 Mpps 59.87 Gbps PCAP: 255.62 Gbps | Out 0.40198 GB Flows/Snap:  43311 FlowCPU:0.42 | ESPush:     0  83.93K ESErr    0 | OutCPU: 0.00 (0.00)
// [11:23:20.340.847.872] In:55.938 GB 6.57 Mpps 60.16 Gbps PCAP: 251.55 Gbps | Out 0.46324 GB Flows/Snap:  43397 FlowCPU:0.42 | ESPush:     0 111.99K ESErr    0 | OutCPU: 0.00 (0.00)
// [11:23:20.572.062.208] In:62.882 GB 6.60 Mpps 59.65 Gbps PCAP: 258.00 Gbps | Out 0.51774 GB Flows/Snap:  44421 FlowCPU:0.41 | ESPush:     0 100.49K ESErr    0 | OutCPU: 0.00 (0.00)
// [11:23:20.802.807.808] In:69.805 GB 6.60 Mpps 59.45 Gbps PCAP: 257.70 Gbps | Out 0.56472 GB Flows/Snap:  41743 FlowCPU:0.41 | ESPush:     0  86.64K ESErr    0 | OutCPU: 0.00 (0.00)
//
// PCAPWall time: 16899884032.00 sec ProcessTime 15.72 sec (0.000)
// Total Time: 15.72 sec RawInput[50.191 Gbps 44169856 Pps] Output[0.390 Gbps] TotalLine:1396322 88838 Line/Sec
//
//---------------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <locale.h>
#include <linux/sched.h>
#include <pthread.h>

#include "fTypes.h"
#include "fProfile.h"
#include "output.h"
#include "flow.h"

void sha1_compress(uint32_t state[static 5], const uint8_t block[static 64]);

//---------------------------------------------------------------------------------------------

typedef struct FlowRecord_t 
{
	u16						EtherProto;			// ethernet protocol
	u8						EtherSrc[6];		// ethernet src mac
	u8						EtherDst[6];		// ethernet dst mac

	u16						VLAN[4];			// vlan tags
	u16						MPLS[4];			// MPLS tags
	u16						MPLStc[4];			// MPLS traffic class 

	u8						IPSrc[4];			// source IP
	u8						IPDst[4];			// source IP

	u8						IPProto;			// IP protocol

	u16						PortSrc;			// tcp/udp port source
	u16						PortDst;			// tcp/udp port source

	u8						pad[17];			// SHA1 calcuated on the first 64B

	u16						TCPACKCnt;			// TCP ACK count within the time period	
	u16						TCPFINCnt;			// TCP FIN count within the time period	
	u16						TCPSYNCnt;			// TCP SYN count within the time period	
	u16						TCPPSHCnt;			// TCP PSH count within the time period	
	u16						TCPRSTCnt;			// TCP RST count within the time period	
	u16						TCPWindowMin;		// TCP Window Minimum 
	u16						TCPWindowMax;		// TCP Window Maximum 

	u16						TCPACKDupCnt;		// number of TCP duplicate acks seen
	u16						TCPSACKCnt;			// number of TCP SACK acknowledgements 

	u32						TCPSeqNo;			// last TCP Seq no seen
	u32						TCPAckNo;			// last TCP Ack no seen
	u32						TCPAckNoCnt;		// number of acks for this seq no 
	u16						TCPLength;			// tcp payload length
	u8						TCPIsSACK;			// if this packet is SACK
	u32						TCPWindowScale;		// tcp window scaling factor

	//-------------------------------------------------------------------------------
	
	u32						SHA1[5];			// SHA of the flow

	u64						FirstTS;			// first TS seen
	u64						LastTS;				// last TS seen 

	u64						TotalPkt;			// total packets
	u64						TotalByte;			// total bytes

	TCPHeader_t				TCPHeader;			// copy of the TCP Header

	struct FlowRecord_t*	Next;				// next flow record
	struct FlowRecord_t*	Prev;				// previous flow record

} __attribute__((packed)) FlowRecord_t;

// top level flow index
typedef struct FlowIndex_t
{
	u64						FlowMax;			// maximum number of flows 
	FlowRecord_t*			FlowList;			// list of statically allocated flows
	FlowRecord_t**			FlowHash;			// flash hash index
	u32						FlowLock;			// mutex to modify 

	u64						FlowCntSnapshot;	// number of flows in this snapshot
	u32						PktBlockCnt;		// current number of processes packet blocks 
	u32						PktBlockMax;		// number of packet blocks in this index
												// valid on root only

	volatile bool			IsUse;

	struct FlowIndex_t*		FreeNext;			// next in free list

} FlowIndex_t;

//---------------------------------------------------------------------------------------------
// command line parameters, see main.c for descriptions
extern bool				g_IsJSONPacket;
extern bool				g_IsJSONFlow;

extern  s64				g_FlowSampleRate;
extern bool				g_IsFlowNULL;

extern u8 				g_CaptureName[256];
extern u8				g_DeviceName[128];

//---------------------------------------------------------------------------------------------
// static

static volatile bool			s_Exit = false;

static u32						s_FlowCntSnapshotLast = 0;				// last total flows in the last snapshot
static u64						s_PktCntSnapshotLast = 0;				// last total number of packets in the snapshot 

static u32						s_FlowIndexMax		= 16;
static u32						s_FlowIndexSub		= 0;				// number of sub slots, one per CPU worker 
static FlowIndex_t				s_FlowIndex[128];
static u32						s_FlowIndexFreeLock	= 0;				// lock to access
static FlowIndex_t*				s_FlowIndexFree		= NULL;				// free list

static u64						s_FlowCntTotal		= 0;				// total number of active flows
static u64						s_PktCntTotal		= 0;				// total number of packets processed 
static u64						s_FlowSampleTSLast	= 0;				// last time the flow was sampled 

static u32						s_PacketBufferMax	= 1024;				// max number of inflight packets
static PacketBuffer_t			s_PacketBufferList[1024];				// list of header structs for each buffer^
static volatile PacketBuffer_t*	s_PacketBuffer		= NULL;				// linked list of free packet buffers
static u32						s_PacketBufferLock	= 0;

static u32						s_DecodeCPUActive 	= 0;				// total number of active decode threads
static pthread_t   				s_DecodeThread[16];						// worker decode thread list
static u64						s_DecodeThreadTSCTop[128];				// total cycles
static u64						s_DecodeThreadTSCDecode[128];			// total cycles for decoding
static u64						s_DecodeThreadTSCInsert[128];			// cycles spend in hash table lookup 
static u64						s_DecodeThreadTSCHash[128];				// cycles spend hashing the flow 
static u64						s_DecodeThreadTSCOutput[128];			// cycles spent in output logic 
static u64						s_DecodeThreadTSCOStall[128];			// cycles spent waiting for an FlowIndex alloc 
static u64						s_DecodeThreadTSCMerge[128];			// cycles spent merging multiple flow indexs 
static u64						s_DecodeThreadTSCWrite[128];			// cycles spent serialzing the json output sprintf dominated 

static volatile u32				s_DecodeQueuePut 	= 0;				// put/get processing queue
static volatile u32				s_DecodeQueueGet 	= 0;
static u32						s_DecodeQueueMax 	= 1024;
static u32						s_DecodeQueueMsk 	= 1023;
static volatile PacketBuffer_t*	s_DecodeQueue[1024];					// list of packets pending processing

static struct Output_t*			s_Output			= NULL;				// output module

static u64						s_PacketQueueCnt	= 0;
static u64						s_PacketDecodeCnt	= 0;

static u32						s_PacketSizeHistoBin = 32;				// size divide amount 
static u32						s_PacketSizeHistoMax = 1024;			// max index (not in byte)
static u32						s_PacketSizeHisto[16][1024];

//---------------------------------------------------------------------------------------------
// generate a 20bit hash index 
static u32 HashIndex(u32* SHA1)
{
	u8* Data8 = (u8*)SHA1;

	// FNV1a 80b hash 
	const u32 Prime  = 0x01000193; //   16777619
	const u32  Seed  = 0x811C9DC5; // 2166136261

	u32 Hash = Seed;
	Hash = ((u32)Data8[ 0] ^ Hash) * Prime;
	Hash = ((u32)Data8[ 1] ^ Hash) * Prime;
	Hash = ((u32)Data8[ 2] ^ Hash) * Prime;
	Hash = ((u32)Data8[ 3] ^ Hash) * Prime;

	Hash = ((u32)Data8[ 4] ^ Hash) * Prime;
	Hash = ((u32)Data8[ 5] ^ Hash) * Prime;
	Hash = ((u32)Data8[ 6] ^ Hash) * Prime;
	Hash = ((u32)Data8[ 7] ^ Hash) * Prime;

	Hash = ((u32)Data8[ 8] ^ Hash) * Prime;
	Hash = ((u32)Data8[ 9] ^ Hash) * Prime;
	Hash = ((u32)Data8[10] ^ Hash) * Prime;
	Hash = ((u32)Data8[11] ^ Hash) * Prime;

	Hash = ((u32)Data8[12] ^ Hash) * Prime;
	Hash = ((u32)Data8[13] ^ Hash) * Prime;
	Hash = ((u32)Data8[14] ^ Hash) * Prime;
	Hash = ((u32)Data8[15] ^ Hash) * Prime;

	Hash = ((u32)Data8[16] ^ Hash) * Prime;
	Hash = ((u32)Data8[17] ^ Hash) * Prime;
	Hash = ((u32)Data8[18] ^ Hash) * Prime;
	Hash = ((u32)Data8[19] ^ Hash) * Prime;

	// reduce down to 20bits for set/way index
	return (Hash & 0x000fffff) ^ (Hash >> 20);
}

//---------------------------------------------------------------------------------------------

static FlowRecord_t* FlowAlloc(FlowIndex_t* FlowIndex, FlowRecord_t* F)
{
	assert(FlowIndex->FlowCntSnapshot < FlowIndex->FlowMax);

	FlowRecord_t* Flow = &FlowIndex->FlowList[ FlowIndex->FlowCntSnapshot++ ]; 
	memset(Flow, 0, sizeof(FlowRecord_t) );

	// copy flow values, leaving the counters reset at zero 
	memcpy(Flow, F, offsetof(FlowRecord_t, pad));

	// copy per packet state
	Flow->TCPLength = F->TCPLength;

	return Flow;
}

//---------------------------------------------------------------------------------------------
// free a flow index 
static void FlowIndexFree(FlowIndex_t* FlowIndexRoot)
{
	for (int i=0; i < s_FlowIndexSub; i++)
	{
		FlowIndex_t* FlowIndex = FlowIndexRoot + i;

		if (!FlowIndex->IsUse)
		{
			printf("ERRROR: IndexFree: %p %i InUse:%i\n", FlowIndexRoot, i, FlowIndex->IsUse);
		}

		memset(FlowIndex->FlowHash, 0, sizeof(FlowRecord_t*) * (2 << 20) );
		FlowIndex->FlowCntSnapshot = 0;

		FlowIndex->PktBlockCnt 	= 0;
		FlowIndex->PktBlockMax 	= 0;

		FlowIndex->IsUse 		= false;
	}

	// append the root FlowIndex into the alloc list
	sync_lock(&s_FlowIndexFreeLock, 100);
	{
		// check index is not already in free list
		bool IsError = false;
		FlowIndex_t* I = s_FlowIndexFree;
		while (I)
		{
			if (I == FlowIndexRoot)
			{
				printf("***ERROR** double entry\n");
				IsError = true;
				break;
			}	
			I = I->FreeNext;
		}

		if (!IsError)
		{
			FlowIndexRoot->FreeNext = s_FlowIndexFree;
			s_FlowIndexFree			= FlowIndexRoot;
		}
	}
	sync_unlock(&s_FlowIndexFreeLock);
}

// allocate a root flow index
static FlowIndex_t* FlowIndexAlloc(void)
{
	FlowIndex_t* F = NULL;
	while (!F)
	{
		sync_lock(&s_FlowIndexFreeLock, 100);
		{

			F = s_FlowIndexFree;
			if (F)
			{
				s_FlowIndexFree = F->FreeNext;
			}
		}
		sync_unlock(&s_FlowIndexFreeLock);
	}
	for (int i=0; i < s_FlowIndexSub; i++)
	{
		(F+i)->IsUse = true;
	}

	return F;
}

//---------------------------------------------------------------------------------------------
// returns the flow entry or creates one in the index
static FlowRecord_t* FlowAdd(FlowIndex_t* FlowIndex, FlowRecord_t* FlowPkt, u32* SHA1)
{
	//u64 TSC0 = rdtsc();

	bool IsFlowNew = false;
	FlowRecord_t* F = NULL;

	u32 Index = HashIndex(SHA1);

	// first record ?
	if (FlowIndex->FlowHash[ Index ] == NULL)
	{
		F = FlowAlloc(FlowIndex, FlowPkt);

		F->SHA1[0] = SHA1[0];
		F->SHA1[1] = SHA1[1];
		F->SHA1[2] = SHA1[2];
		F->SHA1[3] = SHA1[3];
		F->SHA1[4] = SHA1[4];

		F->Next		= NULL;
		F->Prev		= NULL;

		FlowIndex->FlowHash[Index] = F;

		IsFlowNew = true;
	}
	else
	{
		F = FlowIndex->FlowHash[ Index ];

		// iterate in search of the flow
		FlowRecord_t* FPrev = NULL;
		while (F)
		{
			bool IsHit = true;

			IsHit &= (F->SHA1[0] == SHA1[0]);
			IsHit &= (F->SHA1[1] == SHA1[1]);
			IsHit &= (F->SHA1[2] == SHA1[2]);
			IsHit &= (F->SHA1[3] == SHA1[3]);
			IsHit &= (F->SHA1[4] == SHA1[4]);

			if (IsHit)
			{
				break;
			}

			FPrev = F;
			F = F->Next;
		}

		// new flow
		if (!F)
		{
			F = FlowAlloc(FlowIndex, FlowPkt);

			F->SHA1[0] = SHA1[0];
			F->SHA1[1] = SHA1[1];
			F->SHA1[2] = SHA1[2];
			F->SHA1[3] = SHA1[3];
			F->SHA1[4] = SHA1[4];

			F->Next		= NULL;
			F->Prev		= NULL;

			FPrev->Next = F;
			F->Prev		= FPrev;

			IsFlowNew	= true;
		}
	}

	//u64 TSC1 = rdtsc();
	//s_DecodeThreadTSCInsert[CPUID] += TSC1 - TSC0;

	return F;
}

//---------------------------------------------------------------------------------------------
// assumption is this is mutually exclusive per FlowIndex
static void FlowInsert(u32 CPUID, FlowIndex_t* FlowIndex, FlowRecord_t* FlowPkt, u32* SHA1, u32 Length, u64 TS)
{
	// create/fetch the flow entry
	FlowRecord_t* F = FlowAdd(FlowIndex, FlowPkt, SHA1);
	assert(F != NULL);

	// update flow stats
	F->TotalPkt		+= 1;
	F->TotalByte	+= Length;
	F->FirstTS		= (F->FirstTS == 0) ? TS : F->FirstTS;
	F->LastTS		=  TS;

	if (F->IPProto == IPv4_PROTO_TCP)
	{
		// update TCP Flag counts
		TCPHeader_t* TCP = &FlowPkt->TCPHeader; 
		u16 TCPFlags = swap16(TCP->Flags);
		F->TCPFINCnt	+= (TCP_FLAG_FIN(TCPFlags) != 0);
		F->TCPSYNCnt	+= (TCP_FLAG_SYN(TCPFlags) != 0);
		F->TCPRSTCnt	+= (TCP_FLAG_RST(TCPFlags) != 0);
		F->TCPPSHCnt	+= (TCP_FLAG_PSH(TCPFlags) != 0);
		F->TCPACKCnt	+= (TCP_FLAG_ACK(TCPFlags) != 0);

		// check for re-transmits
		// works by checking for duplicate 0 payload acks
		// of an ack no thats already been seen. e.g. tcp fast re-transmit request
		// https://en.wikipedia.org/wiki/TCP_congestion_control#Fast_retransmit
		// 
		// 2018/12/04: SACK traffic messes this up
/*
		if (TCP_FLAG_ACK(TCPFlags))
		{
			u32 TCPAckNo	= swap32(TCP->AckNo);
			if ((FlowPkt->TCPLength == 0) && (F->TCPAckNo == TCPAckNo))
			{
				// if its not a SACK
				if (!FlowPkt->TCPIsSACK)
				{
					F->TCPSACKCnt	+= 1; 
				}
				else
				{
					F->TCPACKDupCnt	+= 1; 
				}
			}
			F->TCPAckNo = TCPAckNo;
		}

		// first packet
		u32 TCPWindow = swap16(TCP->Window); 
		if (F->TotalPkt == 1)
		{
			F->TCPWindowMin = TCPWindow; 
			F->TCPWindowMax = TCPWindow; 
		}
		F->TCPWindowMin = min32(F->TCPWindowMin, TCPWindow);
		F->TCPWindowMax = max32(F->TCPWindowMax, TCPWindow);

*/
	}
}

//---------------------------------------------------------------------------------------------
// write a flow record out as a JSON file
// this is designed for ES bulk data upload using the 
// mappings.json file as the index 
static u64 FlowDump(struct Output_t* Out, u64 TS, FlowRecord_t* Flow, u32 FlowID) 
{
	u8 OutputStr[1024];
	u8* Output 		= OutputStr;
	u8* OutputStart = Output;

	// ES header for bulk upload
	Output += sprintf(Output, "{\"index\":{\"_index\":\"%s\",\"_type\":\"flow_record\",\"_score\":null}}\n", g_CaptureName);

	// as its multi threaded FormatTS can not be used
	u8 TStr[128];
	FormatTSStr(TStr, TS);

	// actual payload
	Output += sprintf(Output, "{\"timestamp\":%f,\"TS\":\"%s\",\"FlowCnt\":%lli,\"Device\":\"%s\"", TS/1e6, TStr, FlowID, g_DeviceName);

	// print flow info
	Output += sprintf(Output, ",\"hash\":\"%08x%08x%08x%08x%08x\"",	Flow->SHA1[0],
																	Flow->SHA1[1],
																	Flow->SHA1[2],
																	Flow->SHA1[3],
																	Flow->SHA1[4]);

	Output += sprintf(Output, ",\"MACSrc\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"MACDst\":\"%02x:%02x:%02x:%02x:%02x:%02x\"",

														Flow->EtherSrc[0],
														Flow->EtherSrc[1],
														Flow->EtherSrc[2],
														Flow->EtherSrc[3],
														Flow->EtherSrc[4],
														Flow->EtherSrc[5],

														Flow->EtherDst[0],
														Flow->EtherDst[1],
														Flow->EtherDst[2],
														Flow->EtherDst[3],
														Flow->EtherDst[4],
														Flow->EtherDst[5]
	);

	// output human readable Ether protocol info
	u8 MACProto[128];
	switch (Flow->EtherProto)
	{
	case ETHER_PROTO_ARP:
		strcpy(MACProto, "ARP");
		break;
	case ETHER_PROTO_IPV4:
		strcpy(MACProto, "IPv4");
		break;
	case ETHER_PROTO_IPV6:
		strcpy(MACProto, "IPv6");
		break;
	case ETHER_PROTO_VLAN:
		strcpy(MACProto, "VLAN");
		break;
	case ETHER_PROTO_VNTAG:
		strcpy(MACProto, "VNTAG");
		break;
	case ETHER_PROTO_MPLS:
		strcpy(MACProto, "MPLS");
		break;
	default:
		sprintf(MACProto, "%04x", Flow->EtherProto);
		break;
	}
	Output += sprintf(Output, ",\"MACProto\":\"%s\"", MACProto); 

	// print VLAN is valid
	if (Flow->VLAN[0] != 0)
	{
		Output += sprintf(Output, ",\"VLAN.0\":%i",  Flow->VLAN[0]);
	}
	if (Flow->VLAN[1] != 0)
	{
		Output += sprintf(Output, ",\"VLAN.1\":%i",  Flow->VLAN[1]);
	}

	// print MPLS info
	if (Flow->MPLS[0])
	{
		Output += sprintf(Output, ",\"MPLS.0.Label\":%i, \"MPLS.0.TC\":%i",  Flow->MPLS[0], Flow->MPLStc[0]);
	}
	if (Flow->MPLS[1])
	{
		Output += sprintf(Output, ",\"MPLS.1.Label\":%i, \"MPLS.1.TC\":%i",  Flow->MPLS[1], Flow->MPLStc[1]);
	}

	// IPv4 proto info
	if (Flow->EtherProto ==  ETHER_PROTO_IPV4)
	{
		Output += sprintf(Output, ",\"IPv4.Src\":\"%i.%i.%i.%i\",\"IPv4.Dst\":\"%i.%i.%i.%i\" ",
											Flow->IPSrc[0],
											Flow->IPSrc[1],
											Flow->IPSrc[2],
											Flow->IPSrc[3],

											Flow->IPDst[0],
											Flow->IPDst[1],
											Flow->IPDst[2],
											Flow->IPDst[3]
		);

		// convert to readable names for common protocols 
		u8 IPProto[128];
		switch (Flow->IPProto) 
		{
		case IPv4_PROTO_UDP:	strcpy(IPProto, "UDP");		break;
		case IPv4_PROTO_TCP:	strcpy(IPProto, "TCP");		break;
		case IPv4_PROTO_IGMP:	strcpy(IPProto, "IGMP"); 	break;
		case IPv4_PROTO_ICMP:	strcpy(IPProto, "ICMP"); 	break;
		case IPv4_PROTO_GRE:	strcpy(IPProto, "GRE"); 	break;
		case IPv4_PROTO_VRRP:	strcpy(IPProto, "VRRP"); 	break;
		default:
			sprintf(IPProto, "%02x", Flow->IPProto);
			break;
		}
		Output += sprintf(Output, ",\"IPv4.Proto\":\"%s\"", IPProto);

		// per protocol info
		switch (Flow->IPProto)
		{
		case IPv4_PROTO_UDP:
		{
			Output += sprintf(Output, ",\"UDP.Port.Src\":%i,\"UDP.Port.Dst\":%i",
												Flow->PortSrc,
												Flow->PortDst	
			);
		}
		break;

		case IPv4_PROTO_TCP:
		{
			if (g_IsJSONPacket)
			{
				TCPHeader_t* TCP = &Flow->TCPHeader; 
				u16 Flags = swap16(TCP->Flags);
				Output += sprintf(Output,",\"TCP.SeqNo\":%u,\"TCP.AckNo\":%u,\"TCP.FIN\":%i,\"TCP.SYN\":%i,\"TCP.RST\":%i,\"TCP.PSH\":%i,\"TCP.ACK\":%i,\"TCP.Window\":%i",
						swap32(TCP->SeqNo),
						swap32(TCP->AckNo),
						TCP_FLAG_FIN(Flags),
						TCP_FLAG_SYN(Flags),
						TCP_FLAG_RST(Flags),
						TCP_FLAG_PSH(Flags),
						TCP_FLAG_ACK(Flags),
						swap16(TCP->Window)
				);
			}
			else
			{
				Output += sprintf(Output,",\"TCP.FIN\":%i,\"TCP.SYN\":%i,\"TCP.RST\":%i,\"TCP.PSH\":%i,\"TCP.ACK\":%i,\"TCP.WindowMin\":%i,\"TCP.WindowMax\":%i,\"TCP.ACKDup\":%i,\"TCP.SACK\":%i",
						Flow->TCPFINCnt,
						Flow->TCPSYNCnt,
						Flow->TCPRSTCnt,
						Flow->TCPPSHCnt,
						Flow->TCPACKCnt,
						Flow->TCPWindowMin,
						Flow->TCPWindowMax,
						Flow->TCPACKDupCnt,
						Flow->TCPSACKCnt
				);
			}
			Output += sprintf(Output, ",\"TCP.Port.Src\":%i,\"TCP.Port.Dst\":%i",
										Flow->PortSrc,
										Flow->PortDst	
			);
		}
		break;
		}
	}

	Output += sprintf(Output, ",\"TotalPkt\":%lli,\"TotalByte\":%lli,\"TotalBits\":%lli",
									Flow->TotalPkt,
									Flow->TotalByte,
									Flow->TotalByte*8ULL
	);

	Output += sprintf(Output, "}\n");

	u32 OutputLen = Output - OutputStart;

	return Output_LineAdd(Out, OutputStart, OutputLen);
}

//---------------------------------------------------------------------------------------------
// merges mutliple flow entries into a single index
// as each CPU flow list gets merged into a single list 
static void FlowMerge(FlowIndex_t* IndexOut, FlowIndex_t* IndexRoot, u32 IndexCnt)
{
	for (int CPU=0; CPU < IndexCnt; CPU++)
	{
		FlowIndex_t* Source = IndexRoot + CPU; 
		if (Source == IndexOut) continue;

		for (int i=0; i < Source->FlowCntSnapshot; i++)
		{
			FlowRecord_t* Flow = &Source->FlowList[i];

			FlowRecord_t* F = FlowAdd(IndexOut, Flow, Flow->SHA1);

			F->TotalPkt 	+= Flow->TotalPkt;
			F->TotalByte 	+= Flow->TotalByte;
			F->FirstTS 		= min64(F->FirstTS, Flow->FirstTS);
			F->LastTS 		= max64(F->LastTS, Flow->LastTS);

			// TCP stats
			if (F->IPProto == IPv4_PROTO_TCP)
			{
				F->TCPFINCnt	+= Flow->TCPFINCnt; 
				F->TCPSYNCnt	+= Flow->TCPSYNCnt; 
				F->TCPRSTCnt	+= Flow->TCPRSTCnt; 
				F->TCPPSHCnt	+= Flow->TCPPSHCnt; 
				F->TCPACKCnt	+= Flow->TCPACKCnt; 

				// Need work out tcp retransmit

				F->TCPWindowMin = min32(F->TCPWindowMin, Flow->TCPWindowMin);
				F->TCPWindowMax = max32(F->TCPWindowMax, Flow->TCPWindowMax);
			}
		}
	}
}

//---------------------------------------------------------------------------------------------
//
// parse a packet and generate a flow record 
//
void DecodePacket(	u32 CPUID,
					struct Output_t* Out, 
					FMADPacket_t* PktHeader, 
					FlowIndex_t* FlowIndex)
{
	FlowRecord_t	sFlow;	
	FlowRecord_t*	FlowPkt = &sFlow;	
	memset(FlowPkt, 0, sizeof(FlowRecord_t));

	// assume single packet flow
	FlowPkt->TotalPkt	 	= 1;
	FlowPkt->TotalByte 		= PktHeader->LengthWire;

	// ether header info
	fEther_t* Ether 		= (fEther_t*)(PktHeader + 1);	
	u8* Payload 			= (u8*)(Ether + 1);
	u16 EtherProto 			= swap16(Ether->Proto);

	FlowPkt->EtherProto		= EtherProto;
	FlowPkt->EtherSrc[0]	= Ether->Src[0];
	FlowPkt->EtherSrc[1]	= Ether->Src[1];
	FlowPkt->EtherSrc[2]	= Ether->Src[2];
	FlowPkt->EtherSrc[3]	= Ether->Src[3];
	FlowPkt->EtherSrc[4]	= Ether->Src[4];
	FlowPkt->EtherSrc[5]	= Ether->Src[5];

	FlowPkt->EtherDst[0]	= Ether->Dst[0];
	FlowPkt->EtherDst[1]	= Ether->Dst[1];
	FlowPkt->EtherDst[2]	= Ether->Dst[2];
	FlowPkt->EtherDst[3]	= Ether->Dst[3];
	FlowPkt->EtherDst[4]	= Ether->Dst[4];
	FlowPkt->EtherDst[5]	= Ether->Dst[5];

	// VLAN decoder
	if (EtherProto == ETHER_PROTO_VLAN)
	{
		VLANTag_t* Header 	= (VLANTag_t*)(Ether+1);
		u16* Proto 			= (u16*)(Header + 1);

		// update to the acutal proto / ipv4 header
		EtherProto 			= swap16(Proto[0]);
		Payload 			= (u8*)(Proto + 1);

		// first vlan tag
		FlowPkt->VLAN[0]		= VLANTag_ID(Header);

		// VNTag unpack (BME) 
		if (EtherProto == ETHER_PROTO_VNTAG)
		{
			VNTag_t* Header = (VNTag_t*)(Proto+1);
			Proto 			= (u16*)(Header + 1);

			// update to the acutal proto / ipv4 header
			EtherProto 		= swap16(Proto[0]);
			Payload 		= (u8*)(Proto + 1);
		}

		// is it double tagged ? 
		if (EtherProto == ETHER_PROTO_VLAN)
		{
			Header 			= (VLANTag_t*)(Proto+1);
			Proto 			= (u16*)(Header + 1);

			// update to the acutal proto / ipv4 header
			EtherProto 		= swap16(Proto[0]);
			Payload 		= (u8*)(Proto + 1);

			// 2nd vlan tag
			FlowPkt->VLAN[1]		= VLANTag_ID(Header);
		}
	}

	// MPLS decoder	
	if (EtherProto == ETHER_PROTO_MPLS)
	{
		MPLSHeader_t* MPLS = (MPLSHeader_t*)(Payload);

		u32 MPLSDepth = 0;

		// first MPLS 
		FlowPkt->MPLS[0]		= MPLS_LABEL(MPLS);
		FlowPkt->MPLStc[0]		= MPLS->TC;

		// for now only process outer tag
		// assume there is a sane limint on the encapsulation count
		if (!MPLS->BOS)
		{
			MPLS += 1;
			MPLSDepth++;

			// seccond 
			FlowPkt->MPLS[1]		= MPLS_LABEL(MPLS);
			FlowPkt->MPLStc[1]		= MPLS->TC;
		}
		if (!MPLS->BOS)
		{
			MPLS += 1;
			MPLSDepth++;

			// third 
			FlowPkt->MPLS[2]		= MPLS_LABEL(MPLS);
			FlowPkt->MPLStc[2]		= MPLS->TC;
		}
		if (!MPLS->BOS)
		{
			MPLS += 1;
			MPLSDepth++;

			// fourth 
			FlowPkt->MPLS[3]		= MPLS_LABEL(MPLS);
			FlowPkt->MPLStc[3]		= MPLS->TC;
		}

		// update to next header
		if (MPLS->BOS)
		{
			EtherProto = ETHER_PROTO_IPV4;
			Payload = (u8*)(MPLS + 1);
		}
	}

	// update final ethernet protocol
	FlowPkt->EtherProto	= EtherProto;

	// ipv4 info
	if (EtherProto == ETHER_PROTO_IPV4)
	{
		IP4Header_t* IP4 = (IP4Header_t*)Payload;
		FlowPkt->IPSrc[0] = IP4->Src.IP[0];	
		FlowPkt->IPSrc[1] = IP4->Src.IP[1];	
		FlowPkt->IPSrc[2] = IP4->Src.IP[2];	
		FlowPkt->IPSrc[3] = IP4->Src.IP[3];	

		FlowPkt->IPDst[0] = IP4->Dst.IP[0];	
		FlowPkt->IPDst[1] = IP4->Dst.IP[1];	
		FlowPkt->IPDst[2] = IP4->Dst.IP[2];	
		FlowPkt->IPDst[3] = IP4->Dst.IP[3];	

		FlowPkt->IPProto = IP4->Proto;

		// IPv4 protocol decoders 
		u32 IPOffset = (IP4->Version & 0x0f)*4; 
		switch (IP4->Proto)
		{
		case IPv4_PROTO_TCP:
		{
			TCPHeader_t* TCP = (TCPHeader_t*)(Payload + IPOffset);

			// ensure TCP data is valid and not hashing 
			// random data in memeory
			if (((u8*)TCP - (u8*)Ether) + sizeof(TCPHeader_t) < PktHeader->LengthCapture)
			{
				FlowPkt->PortSrc	= swap16(TCP->PortSrc);
				FlowPkt->PortDst	= swap16(TCP->PortDst);

				// make a copy of the tcp header 
				FlowPkt->TCPHeader = TCP[0];

				// payload length
				u32 TCPOffset = ((TCP->Flags&0xf0)>>4)*4;
				FlowPkt->TCPLength =  swap16(IP4->Len) - IPOffset - TCPOffset;

				// check for options
				if (TCPOffset > 20)
				{
					bool IsDone = false;
					u8* Options = (u8*)(TCP + 1);	
					while ( (Options - (u8*)TCP) < TCPOffset) 
					{
						if (IsDone) break;

						u32 Cmd = Options[0];
						u32 Len = Options[1];

						switch (Cmd)
						{
						// end of list 
						case 0x0:
							IsDone = true;
							break;

						// NOP 
						case 0x1: break;

						// MSS
						case 0x2: break;

						// Window Scale
						case 0x3:
							//printf("Window Scale: %i\n", Options[2]);
							FlowPkt->TCPWindowScale = Options[2];
							break;

						// SACK
						case 0x5:
							FlowPkt->TCPIsSACK = true;
							break;

						// TSOpt
						case 0x8: 
							//printf("TCP Option TS\n");
							break;

						default:
							//printf("option: %i : %i\n", Cmd, Len); 
							break;
						}
						Options += 1 + Len;
					}
				}
			}
		}
		break;

		case IPv4_PROTO_UDP:
		{
			UDPHeader_t* UDP = (UDPHeader_t*)(Payload + IPOffset);

			FlowPkt->PortSrc	= swap16(UDP->PortSrc);
			FlowPkt->PortDst	= swap16(UDP->PortDst);
		}
		break;
		}
	}

	// generate SHA1
	// nice way to grab all packets for a single flow, search for the sha1 hash	
	// NOTE: FlowPktRecord_t setup so the first 64B contains only the flow info
	//       with packet and housekeeping info stored after. sha1_compress
	//       runs on the first 64B only 
	u64 TSC0 = rdtsc();

	u32 SHA1State[5] = { 0, 0, 0, 0, 0 };
	sha1_compress(SHA1State, (u8*)FlowPkt);

	FlowPkt->SHA1[0] = SHA1State[0];
	FlowPkt->SHA1[1] = SHA1State[1];
	FlowPkt->SHA1[2] = SHA1State[2];
	FlowPkt->SHA1[3] = SHA1State[3];
	FlowPkt->SHA1[4] = SHA1State[4];

	u64 TSC1 = rdtsc();
	s_DecodeThreadTSCHash[CPUID] += TSC1 - TSC0;

	// packet mode then print record as a packet 
	if (g_IsJSONPacket)
	{
		FlowDump(Out, PktHeader->TS, FlowPkt, 0);
	}
	// update the flow records
	if (g_IsJSONFlow)
	{
		// insert to flow table
		// NOTE: each CPU has its own FlowIndex no need to lock it 
		FlowInsert(CPUID, FlowIndex, FlowPkt, SHA1State, PktHeader->LengthWire, PktHeader->TS);
	}
}

//---------------------------------------------------------------------------------------------
// queue a packet for processing 
static FlowIndex_t* s_FlowIndexQueue = NULL;
void Flow_PacketQueue(PacketBuffer_t* Pkt)
{
	// multi-core version
	if (!g_IsFlowNULL)
	{
		// wait for space int he queue 
		u32 Timeout = 0; 
		while ((s_DecodeQueuePut  - s_DecodeQueueGet) >= (s_DecodeQueueMax - s_DecodeCPUActive - 4))
		{
			ndelay(250);
			//usleep(0);
			assert(Timeout++ < 1e9);
		}

		// add to processing queue
		s_DecodeQueue[s_DecodeQueuePut & s_DecodeQueueMsk] 	= Pkt;

		// allocate a new flow
		if (s_FlowIndexQueue == NULL)
		{
			s_FlowIndexQueue = FlowIndexAlloc();
		}

		// set the flow index
		Pkt->IsFlowIndexDump	 = false;
		Pkt->FlowIndex			 = s_FlowIndexQueue; 

		Pkt->ID					= s_DecodeQueuePut;

		// set the total number of pktblocks for this index
		// so merge thread can block until all pktblocks have finished
		// processing
		Pkt->FlowIndex->PktBlockMax	+= 1;

		// purge the flow records every 100msec
		// as this is the singled threaded serialized
		// entry point, can flag it here instead of
		// in the worker threads
		if (s_FlowSampleTSLast  == 0)
		{
			s_FlowSampleTSLast  = (u64)(Pkt->TSLast / g_FlowSampleRate) * g_FlowSampleRate;
		}

		// time to dump the index
		s64 dTS = Pkt->TSLast - s_FlowSampleTSLast;
		if (dTS > g_FlowSampleRate)
		{
			Pkt->IsFlowIndexDump	= true;
			Pkt->TSSnapshot	 		= s_FlowSampleTSLast;

			// add next snapshot time 
			s_FlowSampleTSLast 		+= g_FlowSampleRate; 

			// force new allocation on next Queue 
			s_FlowIndexQueue = NULL;
		}

		s_DecodeQueuePut++;
		s_PacketQueueCnt++;
	}
	else
	{
		// benchmarking mode just release the buffer
		Flow_PacketFree(Pkt);
	}
}

//---------------------------------------------------------------------------------------------

void* Flow_Worker(void* User)
{
	u32 CPUID = __sync_fetch_and_add(&s_DecodeCPUActive, 1);

	FlowIndex_t* FlowIndexLast = NULL;

	printf("Start decoder thread: %i\n", CPUID);
	while (!s_Exit)
	{
		u64 TSC0 = rdtsc();

		u32 Get = s_DecodeQueueGet;
		if (Get == s_DecodeQueuePut)
		{
			// nothing to do
			//ndelay(100);
			usleep(0);
		}
		else
		{
			// fetch the to be processed pkt *before* atomic lock 
			PacketBuffer_t* PktBlock = (PacketBuffer_t*)s_DecodeQueue[Get & s_DecodeQueueMsk];
			assert(PktBlock != NULL);

			// get the entry atomically 
			if (__sync_bool_compare_and_swap(&s_DecodeQueueGet, Get, Get + 1))
			{
// back pressure testing
//u32 delay =  ((u64)rand() * 100000000ULL) / (u64)RAND_MAX; 
//ndelay(delay);
				u64 TSC2 = rdtsc();

				// ensure no sync problems
				assert(PktBlock->ID == Get);

				// root index
				FlowIndex_t* FlowIndexRoot = PktBlock->FlowIndex;

				// assigned index to add the packet to
				FlowIndex_t* FlowIndex = FlowIndexRoot + CPUID; 

				// process all packets in this block 
				u32 Offset = 0;
				u64 TSLast = 0;
				for (int i=0; i < PktBlock->PktCnt; i++)
				{
					FMADPacket_t* PktHeader = (FMADPacket_t*)(PktBlock->Buffer + Offset);
					Offset += sizeof(PCAPPacket_t) + PktHeader->LengthCapture;

					assert(PktHeader->LengthWire    > 0); 
					assert(PktHeader->LengthCapture	> 0); 

					assert(PktHeader->LengthWire    < 16*1024);
					assert(PktHeader->LengthCapture < 16*1024);

					// process the packet
					DecodePacket(CPUID, s_Output, PktHeader, FlowIndex);

					// update size histo
					u32 SizeIndex = (PktHeader->LengthWire / s_PacketSizeHistoBin);
					if (SizeIndex >= s_PacketSizeHistoMax) SizeIndex = s_PacketSizeHistoMax - 1;
					s_PacketSizeHisto[CPUID][SizeIndex]++;

				}

				__sync_fetch_and_add(&s_PktCntTotal, PktBlock->PktCnt);

				// output the snapshot  
				if (g_IsJSONFlow & PktBlock->IsFlowIndexDump)
				{
					// write to output
					u64 TSC0 = rdtsc();

					// wait for all workers to complete
					// ensures workers processing on Indexs 
					// that need to be aggregated into this one
					// have completed processing
					u32 Timeout = 0; 
					while (FlowIndexRoot->PktBlockCnt < FlowIndexRoot->PktBlockMax - 1)
					{
						usleep(0);
						assert(Timeout++ < 1e6);	
					}
					u64 TSC1 = rdtsc();

					// merge everything into this CPUs index 
					// merge flows
					FlowMerge(FlowIndex, FlowIndexRoot, s_FlowIndexSub);

					u64 TSC2 = rdtsc();

					// dump flows
					u64 StallTSC = 0;
					u64 TotalPkt = 0;
					for (int i=0; i < FlowIndex->FlowCntSnapshot; i++)
					{
						FlowRecord_t* Flow = &FlowIndex->FlowList[i];	
						StallTSC += FlowDump(s_Output, PktBlock->TSSnapshot, Flow, i);

						TotalPkt += Flow->TotalPkt;
					}

					// add total number of flows output 
					s_FlowCntTotal += FlowIndex->FlowCntSnapshot;

					// save total merged flow count 
					s_FlowCntSnapshotLast = FlowIndex->FlowCntSnapshot;
					s_PktCntSnapshotLast  = TotalPkt; 

					u64 TSC3 = rdtsc();
					//assert(FlowIndexRoot->IsUse == true);

					// release the root index + cpu subs 
					FlowIndexFree(FlowIndexRoot);

					u64 TSC4 						= rdtsc();
					s_DecodeThreadTSCOutput[CPUID] += TSC4 - TSC0;
					s_DecodeThreadTSCOStall[CPUID] += StallTSC;
					s_DecodeThreadTSCMerge[CPUID]  += TSC2 - TSC1; 
					s_DecodeThreadTSCWrite[CPUID]  += TSC3 - TSC2; 
				}

				// update pktblock count for the root index
				__sync_fetch_and_add(&FlowIndexRoot->PktBlockCnt, 1);

				// release buffer
				Flow_PacketFree(PktBlock);

				// update counter
				__sync_fetch_and_add(&s_PacketDecodeCnt, 1);

				// cpu usage stats
				u64 TSC3 = rdtsc();
				s_DecodeThreadTSCDecode[CPUID] += TSC3 - TSC2;
			}
		}

		u64 TSC1 = rdtsc();
		s_DecodeThreadTSCTop[CPUID] += TSC1 - TSC0;
	}
}

//---------------------------------------------------------------------------------------------
// packet buffer management 
PacketBuffer_t* Flow_PacketAlloc(void)
{
	// stall waiting for free buffer
	u32 Timeout = 0;
	while (true)
	{
		if (s_PacketBuffer != NULL) break;

		usleep(0);
		//ndelay(100);
		assert(Timeout++ < 1e6);
	}

	// acquire lock
	PacketBuffer_t* B = NULL; 
	sync_lock(&s_PacketBufferLock, 50);
	{
		B = (PacketBuffer_t*)s_PacketBuffer;
		assert(B != NULL);

		s_PacketBuffer = B->FreeNext;

		// release lock
		assert(s_PacketBufferLock == 1); 
	}
	sync_unlock(&s_PacketBufferLock);

	// double check its a valid free pkt
	assert(B->IsUsed == false);
	B->IsUsed = true;

	// reset stats
	B->PktCnt			= 0;
	B->ByteWire			= 0;
	B->ByteCapture		= 0;
	B->TSFirst			= 0;
	B->TSLast			= 0;
	B->IsFlowIndexDump	= false;

	return B;
}

void Flow_PacketFree(PacketBuffer_t* B)
{
	// acquire lock
	sync_lock(&s_PacketBufferLock, 100); 
	{
		// push at head
		B->FreeNext 	= (PacketBuffer_t*)s_PacketBuffer;
		s_PacketBuffer 	= B;

		B->IsUsed = false;

		// release lock
		assert(s_PacketBufferLock == 1); 
	}
	sync_unlock(&s_PacketBufferLock);
}

//---------------------------------------------------------------------------------------------
// allocate memory and house keeping
void Flow_Open(struct Output_t* Out, s32* CPUMap)
{
	s_Output = Out;
	assert(s_Output != NULL);

	// allocate packet buffers
	for (int i=0; i < s_PacketBufferMax; i++)
	{
		PacketBuffer_t* B = &s_PacketBufferList[i];
		memset(B, 0, sizeof(PacketBuffer_t));

		B->BufferMax 	= 256*1024 + 1024;
		B->Buffer 		= memalign(4096, B->BufferMax);
		memset(B->Buffer, 0, B->BufferMax);	

		Flow_PacketFree(B);
	}

	// create worker threads
	u32 CPUCnt = 0;

	pthread_create(&s_DecodeThread[ 0], NULL, Flow_Worker, (void*)NULL); CPUCnt++;
	pthread_create(&s_DecodeThread[ 1], NULL, Flow_Worker, (void*)NULL); CPUCnt++;
	pthread_create(&s_DecodeThread[ 2], NULL, Flow_Worker, (void*)NULL); CPUCnt++;
	pthread_create(&s_DecodeThread[ 3], NULL, Flow_Worker, (void*)NULL); CPUCnt++;

	pthread_create(&s_DecodeThread[ 4], NULL, Flow_Worker, (void*)NULL); CPUCnt++;
	pthread_create(&s_DecodeThread[ 5], NULL, Flow_Worker, (void*)NULL); CPUCnt++;
	pthread_create(&s_DecodeThread[ 6], NULL, Flow_Worker, (void*)NULL); CPUCnt++;
	pthread_create(&s_DecodeThread[ 7], NULL, Flow_Worker, (void*)NULL); CPUCnt++;

	for (int i=0; i < CPUCnt; i++)
	{
		if (CPUMap[i] <= 0) continue; 
		
		cpu_set_t Thread0CPU;
		CPU_ZERO(&Thread0CPU);
		CPU_SET (CPUMap[i], &Thread0CPU);
		pthread_setaffinity_np(s_DecodeThread[i], sizeof(cpu_set_t), &Thread0CPU);
	}

	s_FlowIndexMax = 4 * CPUCnt; 
	s_FlowIndexSub = CPUCnt; 

	// allocate flow indexes
	for (int i=0; i < s_FlowIndexMax; i++)
	{
		FlowIndex_t* FlowIndex = &s_FlowIndex[i];

		// max out at 1M flows
		FlowIndex->FlowMax	= 1024*1024;

		// allocate and clear flow index
		FlowIndex->FlowHash = (FlowRecord_t **)memalign(4096, sizeof(FlowRecord_t *) * (2 << 20) );
		assert(FlowIndex->FlowHash != NULL);

		// allocate statically allocated flow list
		FlowIndex->FlowList = (FlowRecord_t *)memalign (4096, sizeof(FlowRecord_t) * FlowIndex->FlowMax );
		assert(FlowIndex->FlowList != NULL);
	}
	// push to free list
	for (int i=0; i < s_FlowIndexMax; i += s_FlowIndexSub)
	{
		FlowIndex_t* FlowIndex = &s_FlowIndex[i];
		for (int j=0; j < s_FlowIndexSub; j++)
		{
			(FlowIndex+j)->IsUse = true;
		}
		FlowIndexFree(FlowIndex);
	}

	// reset histogram
	memset(s_PacketSizeHisto, 0, sizeof(s_PacketSizeHisto));
}

//---------------------------------------------------------------------------------------------
// shutdown / flush
void Flow_Close(struct Output_t* Out, u64 LastTS)
{
	// push a flush packet
	// this ensures the last flow data gets flushed to the output queue
	// in a fully pipelined manner 
	PacketBuffer_t*	PktBlock 	= Flow_PacketAlloc();
	PktBlock->IsFlowIndexDump	= false;
	Flow_PacketQueue(PktBlock);

	// wait for all queues to drain
	u32 Timeout = 0;
	while (s_DecodeQueuePut != s_DecodeQueueGet)
	{
		usleep(0);
		assert(Timeout++ < 1e6);
	}
	s_Exit = true;

	printf("QueueCnt : %lli\n", s_PacketQueueCnt);	
	printf("DecodeCnt: %lli\n", s_PacketDecodeCnt);	

	for (int i=0; i < s_DecodeCPUActive; i++)
	{
		printf("Flow Worker Join %i\n", i);
		pthread_join(s_DecodeThread[i], NULL);
	}
	printf("Flow Close\n");
}

//---------------------------------------------------------------------------------------------

void Flow_Stats(	bool IsReset, 
					u64* pFlowCntSnapShot, 
					u64* pPktCntSnapShot, 
					u64* pFlowCntTotal, 
					float * pCPUDecode,
					float * pCPUHash,
					float * pCPUOutput,
					float * pCPUOStall,
					float * pCPUMerge,
					float * pCPUWrite
){
	if (pFlowCntSnapShot)	pFlowCntSnapShot[0] = s_FlowCntSnapshotLast;
	if (pPktCntSnapShot)	pPktCntSnapShot[0]	= s_PktCntSnapshotLast;

	if (pFlowCntTotal)		pFlowCntTotal[0]	= s_FlowCntTotal;

	u64 TotalTSC 	= 0;
	u64 DecodeTSC 	= 0;
	u64 HashTSC  	= 0;
	u64 OutputTSC	= 0;
	u64 OStallTSC	= 0;
	u64 MergeTSC	= 0;
	u64 WriteTSC	= 0;
	for (int i=0; i < s_DecodeCPUActive; i++)
	{
		TotalTSC 	+= s_DecodeThreadTSCTop		[i];
		DecodeTSC 	+= s_DecodeThreadTSCDecode	[i];
		HashTSC 	+= s_DecodeThreadTSCHash	[i];
		OutputTSC 	+= s_DecodeThreadTSCOutput	[i];
		OStallTSC 	+= s_DecodeThreadTSCOStall	[i];
		MergeTSC 	+= s_DecodeThreadTSCMerge	[i];
		WriteTSC 	+= s_DecodeThreadTSCWrite	[i];
	}

	if (IsReset)
	{
		for (int i=0; i < s_DecodeCPUActive; i++)
		{
			s_DecodeThreadTSCTop[i]		= 0;
			s_DecodeThreadTSCDecode[i]	= 0;
			s_DecodeThreadTSCHash[i]	= 0;
			s_DecodeThreadTSCOutput[i]	= 0;
			s_DecodeThreadTSCOStall[i]	= 0;
			s_DecodeThreadTSCMerge[i]	= 0;
			s_DecodeThreadTSCWrite[i]	= 0;
		}
	}

	if (pCPUDecode) pCPUDecode[0] 	= DecodeTSC * inverse(TotalTSC);
	if (pCPUHash) 	pCPUHash[0] 	= HashTSC 	* inverse(TotalTSC);
	if (pCPUOutput) pCPUOutput[0] 	= OutputTSC * inverse(TotalTSC);
	if (pCPUOStall) pCPUOStall[0] 	= OStallTSC	* inverse(TotalTSC);
	if (pCPUMerge)  pCPUMerge[0] 	= MergeTSC	* inverse(TotalTSC);
	if (pCPUWrite)  pCPUWrite[0] 	= WriteTSC	* inverse(TotalTSC);

	fprintf(stderr, "total packts: %lli\n", s_PktCntTotal);
}

//---------------------------------------------------------------------------------------------
// dump + reset the packet size histogram
void Flow_PktSizeHisto(void)
{
	fprintf(stderr, "Packet Size Histogram:\n");	

	// merge into single histogram
	u32 TotalSamples = 0;
	u32 SizeHisto[1024];
	memset(SizeHisto, 0, sizeof(SizeHisto));
	for (int c=0; c < s_DecodeCPUActive; c++)
	{
		for (int i=0; i < s_PacketSizeHistoMax; i++)
		{
			u32 Cnt = s_PacketSizeHisto[c][i];
			SizeHisto[i] += Cnt; 
			TotalSamples += Cnt; 
		}
	}
	memset(s_PacketSizeHisto, 0, sizeof(s_PacketSizeHisto));

	// find max per bin
	u32 MaxCnt = 0;
	for (int i=0; i < s_PacketSizeHistoMax; i++)
	{
		MaxCnt = (MaxCnt < SizeHisto[i]) ? SizeHisto[i] : MaxCnt;
	}
	float iMaxCnt = inverse(MaxCnt);	
	float iTotal = inverse(TotalSamples);

	// print status
	float CDF = 0;
	for (int i=0; i < s_PacketSizeHistoMax; i++)
	{
		u32 Size = i * s_PacketSizeHistoBin;
		if (SizeHisto[i] == 0) continue;

		CDF  += SizeHisto[i] * iTotal;

		fprintf(stderr, "%5i : %.3f : %10i (%.3f) : %.3f : ", Size, CDF, SizeHisto[i], SizeHisto[i] * iTotal);
		u32 Cnt = (SizeHisto[i] * 80 ) * iMaxCnt;
		for (int j=0; j < Cnt; j++) fprintf(stderr, "*");

		fprintf(stderr, "\n");
	}
	fprintf(stderr, "\n");
}

/* vim: set ts=4 sts=4 */
