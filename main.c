//---------------------------------------------------------------------------------------------
//
// Copyright (c) 2018, fmad engineering llc 
//
// The MIT License (MIT) see LICENSE file for details 
// 
// PCAP to JSON file conversion. convers a PCAP and extracts basic IP / TCP / UDP information
// that can be fed into Elastic Search for further processing and analysis 
//
//---------------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <locale.h>
#include <linux/sched.h>
#include <pthread.h>

#include "fTypes.h"

double TSC2Nano = 0;


void sha1_compress(uint32_t state[static 5], const uint8_t block[static 64]);

//---------------------------------------------------------------------------------------------

typedef struct FlowRecord_t 
{
	u16						EtherProto;			// ethernet protocol
	u8						EtherSrc[6];		// ethernet src mac
	u8						EtherDst[6];		// ethernet dst mac

	u16						VLAN[4];			// vlan tags
	u16						MPLS[4];			// MPLS tags

	u8						IPSrc[4];			// source IP
	u8						IPDst[4];			// source IP

	u8						IPProto;			// IP protocol

	u16						PortSrc;			// tcp/udp port source
	u16						PortDst;			// tcp/udp port source

	u8						pad[21];			// SHA1 calcuated on the first 64B

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

//---------------------------------------------------------------------------------------------
// tunables
bool					g_Verbose		= false;				// verbose print mode

static bool				s_IsJSONPacket	= false;				// output JSON packet format
static bool				s_IsJSONFlow	= false;				// output JSON flow format

static u64				s_FlowMax		= 4*1024*1024;			// maximum number of flows 
static u64				s_FlowCnt		= 0;					// total number of flows
static FlowRecord_t*	s_FlowList		= NULL;					// list of statically allocated flows

static FlowRecord_t**	s_FlowHash		= NULL;					// flash hash index

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

static FlowRecord_t* FlowAlloc(void)
{
	assert(s_FlowCnt < s_FlowMax);

	FlowRecord_t* Flow = &s_FlowList[ s_FlowCnt ]; 
	memset(Flow, 0, sizeof(FlowRecord_t) );

	s_FlowCnt++;

	return Flow;
}

//---------------------------------------------------------------------------------------------

static void FlowInsert(FlowRecord_t* Flow, u32* SHA1, u32 Length, u64 TS)
{
	u32 Index = HashIndex(SHA1);

	FlowRecord_t* F = NULL;

	bool IsFlowNew = false;

	// first record ?
	if (s_FlowHash[ Index ] == NULL)
	{
		F = FlowAlloc();

		memcpy(F, Flow, sizeof(FlowRecord_t));

		F->SHA1[0] = SHA1[0];
		F->SHA1[1] = SHA1[1];
		F->SHA1[2] = SHA1[2];
		F->SHA1[3] = SHA1[3];
		F->SHA1[4] = SHA1[4];

		F->Next		= NULL;
		F->Prev		= NULL;

		s_FlowHash[Index] = F;

		IsFlowNew = true;
	}
	else
	{
		F = s_FlowHash[ Index ];

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
			F = FlowAlloc();

			memcpy(F, Flow, sizeof(FlowRecord_t));

			F->SHA1[0] = SHA1[0];
			F->SHA1[1] = SHA1[1];
			F->SHA1[2] = SHA1[2];
			F->SHA1[3] = SHA1[3];
			F->SHA1[4] = SHA1[4];

			F->Next		= NULL;
			F->Prev		= NULL;

			FPrev->Next = F;
			F->Prev		= FPrev;

			IsFlowNew = true;
		}
	}

	// update flow stats
	F->TotalPkt		+= 1;
	F->TotalByte	+= Length;
	F->FirstTS		= (F->FirstTS == 0) ? TS : F->FirstTS;
	F->LastTS		=  TS;
}

//---------------------------------------------------------------------------------------------
// write a flow record out as a JSON file
// this is designed for ES bulk data upload using the 
// mappings.json file as the index 

static void FlowDump(FILE* FileOut, u8* DeviceName, u8* IndexName, u64 TS, FlowRecord_t* Flow) 
{
	// ES header for bulk upload
	fprintf(FileOut, "{\"index\":{\"_index\":\"%s\",\"_type\":\"flow_record\",\"_score\":null}}\n", IndexName);

	// actual payload
	fprintf(FileOut, "{\"timestamp\":%f,\"TS\":\"%s\",\"FlowCnt\":%lli,\"Device\":\"%s\"", TS/1e6, FormatTS(TS), s_FlowCnt, DeviceName);

	// print flow info
	fprintf(FileOut, ",\"hash\":\"%08x%08x%08x%08x%08x\"",	Flow->SHA1[0],
															Flow->SHA1[1],
															Flow->SHA1[2],
															Flow->SHA1[3],
															Flow->SHA1[4]);

	fprintf(FileOut, ",\"MACSrc\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"MACDst\":\"%02x:%02x:%02x:%02x:%02x:%02x\"",

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
	fprintf(FileOut, ",\"MACProto\":\"%s\"", MACProto); 

	// print VLAN is valid
	if (Flow->VLAN[0] != 0)
	{
		fprintf(FileOut, ",\"VLAN.0\":%i",  Flow->VLAN[0]);
	}
	if (Flow->VLAN[1] != 0)
	{
		fprintf(FileOut, ",\"VLAN.1\":%i",  Flow->VLAN[1]);
	}

	// print MPLS info
	if (Flow->MPLS[0])
	{
		fprintf(FileOut, ",\"MPLS.0\":%i",  Flow->MPLS[0]);
	}
	if (Flow->MPLS[1])
	{
		fprintf(FileOut, ",\"MPLS.1\":%i",  Flow->MPLS[1]);
	}

	// IPv4 proto info
	if (Flow->EtherProto ==  ETHER_PROTO_IPV4)
	{
		fprintf(FileOut, ",\"IPv4.Src\":\"%i.%i.%i.%i\",\"IPv4.Dst\":\"%i.%i.%i.%i\" ",
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
		fprintf(FileOut, ",\"IPv4.Proto\":\"%s\"", IPProto);

		// per protocol info
		switch (Flow->IPProto)
		{
		case IPv4_PROTO_UDP:
		{
			fprintf(FileOut, ",\"UDP.Port.Src\":%i,\"UDP.Port.Dst\":%i",
											Flow->PortSrc,
											Flow->PortDst	
			);
		}
		break;

		case IPv4_PROTO_TCP:
		{
			if (s_IsJSONPacket)
			{
				TCPHeader_t* TCP = &Flow->TCPHeader; 
				u16 Flags = swap16(TCP->Flags);
				fprintf(FileOut,",\"TCP.SeqNo\":%u,\"TCP.AckNo\":%u,\"TCP.FIN\":%i,\"TCP.SYN\":%i,\"TCP.RST\":%i,\"TCP.PSH\":%i,\"TCP.ACK\":%i,\"TCP.Window\":%i",
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
			fprintf(FileOut, ",\"TCP.Port.Src\":%i,\"TCP.Port.Dst\":%i",
										Flow->PortSrc,
										Flow->PortDst	
			);
		}
		break;
		}
	}

	fprintf(FileOut, ",\"TotalPkt\":%lli,\"TotalByte\":%lli",
									Flow->TotalPkt,
									Flow->TotalByte	
	);

	fprintf(FileOut, "}\n");
}

//---------------------------------------------------------------------------------------------
// clear out the flow records 
static void FlowReset(void)
{
	memset(s_FlowHash, 0, sizeof(FlowRecord_t*) * (2 << 20) );
	memset(s_FlowList, 0, sizeof(FlowRecord_t) * s_FlowMax );

	s_FlowCnt = 0;
}

//---------------------------------------------------------------------------------------------
//
// parse a packet and generate a flow record 
//
static void DecodePacket(FILE* FileOut, u8* DeviceName, u8* CaptureName, u64 PacketTS, PCAPPacket_t* PktHeader)
{
	FlowRecord_t	sFlow;	
	FlowRecord_t*	Flow = &sFlow;	
	memset(Flow, 0, sizeof(FlowRecord_t));

	// ether header info
	fEther_t* Ether = (fEther_t*)(PktHeader + 1);	
	u8* Payload 	= (u8*)(Ether + 1);
	u16 EtherProto 	= swap16(Ether->Proto);

	Flow->EtherProto	= EtherProto;
	Flow->EtherSrc[0]	= Ether->Src[0];
	Flow->EtherSrc[1]	= Ether->Src[1];
	Flow->EtherSrc[2]	= Ether->Src[2];
	Flow->EtherSrc[3]	= Ether->Src[3];
	Flow->EtherSrc[4]	= Ether->Src[4];
	Flow->EtherSrc[5]	= Ether->Src[5];

	Flow->EtherDst[0]	= Ether->Dst[0];
	Flow->EtherDst[1]	= Ether->Dst[1];
	Flow->EtherDst[2]	= Ether->Dst[2];
	Flow->EtherDst[3]	= Ether->Dst[3];
	Flow->EtherDst[4]	= Ether->Dst[4];
	Flow->EtherDst[5]	= Ether->Dst[5];
	
	// VLAN decoder
	if (EtherProto == ETHER_PROTO_VLAN)
	{
		VLANTag_t* Header 	= (VLANTag_t*)(Ether+1);
		u16* Proto 			= (u16*)(Header + 1);

		// update to the acutal proto / ipv4 header
		EtherProto 			= swap16(Proto[0]);
		Payload 			= (u8*)(Proto + 1);

		// first vlan tag
		Flow->VLAN[0]		= VLANTag_ID(Header);

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
			Flow->VLAN[1]		= VLANTag_ID(Header);
		}
	}

	// MPLS decoder	
	if (EtherProto == ETHER_PROTO_MPLS)
	{
		MPLSHeader_t* MPLS = (MPLSHeader_t*)(Payload);

		u32 MPLSDepth = 0;

		// first MPLS 
		Flow->MPLS[0]		= MPLS_LABEL(MPLS);

		// for now only process outer tag
		// assume there is a sane limint on the encapsulation count
		if (!MPLS->BOS)
		{
			MPLS += 1;
			MPLSDepth++;

			// seccond 
			Flow->MPLS[1]		= MPLS_LABEL(MPLS);
		}
		if (!MPLS->BOS)
		{
			MPLS += 1;
			MPLSDepth++;

			// third 
			Flow->MPLS[2]		= MPLS_LABEL(MPLS);
		}
		if (!MPLS->BOS)
		{
			MPLS += 1;
			MPLSDepth++;

			// fourth 
			Flow->MPLS[3]		= MPLS_LABEL(MPLS);
		}

		// update to next header
		if (MPLS->BOS)
		{
			EtherProto = ETHER_PROTO_IPV4;
			Payload = (u8*)(MPLS + 1);
		}
	}

	// update final ethernet protocol
	Flow->EtherProto	= EtherProto;

	// ipv4 info
	if (EtherProto == ETHER_PROTO_IPV4)
	{
		IP4Header_t* IP4 = (IP4Header_t*)Payload;

		Flow->IPSrc[0] = IP4->Src.IP[0];	
		Flow->IPSrc[1] = IP4->Src.IP[1];	
		Flow->IPSrc[2] = IP4->Src.IP[2];	
		Flow->IPSrc[3] = IP4->Src.IP[3];	

		Flow->IPDst[0] = IP4->Dst.IP[0];	
		Flow->IPDst[1] = IP4->Dst.IP[1];	
		Flow->IPDst[2] = IP4->Dst.IP[2];	
		Flow->IPDst[3] = IP4->Dst.IP[3];	

		Flow->IPProto = IP4->Proto;

		// IPv4 protocol decoders 
		u32 IPOffset = (IP4->Version & 0x0f)*4; 
		switch (IP4->Proto)
		{
		case IPv4_PROTO_TCP:
		{
			TCPHeader_t* TCP = (TCPHeader_t*)(Payload + IPOffset);

			Flow->PortSrc	= swap16(TCP->PortSrc);
			Flow->PortDst	= swap16(TCP->PortDst);

			// make a copy of the tcp header 
			Flow->TCPHeader = TCP[0];
		}
		break;
		case IPv4_PROTO_UDP:
		{
			UDPHeader_t* UDP = (UDPHeader_t*)(Payload + IPOffset);

			Flow->PortSrc	= swap16(UDP->PortSrc);
			Flow->PortDst	= swap16(UDP->PortDst);
		}
		break;
		}
	}

	// generate SHA1
	// nice way to grab all packets for a single flow, search for the sha1 hash	
	// NOTE: FlowRecord_t setup so the first 64B contains only the flow info
	//       with packet and housekeeping info stored after. sha1_compress
	//       runs on the first 64B only 
	u32 SHA1State[5] = { 0, 0, 0, 0, 0 };
	sha1_compress(SHA1State, (u8*)Flow);

	Flow->SHA1[0] = SHA1State[0];
	Flow->SHA1[1] = SHA1State[1];
	Flow->SHA1[2] = SHA1State[2];
	Flow->SHA1[3] = SHA1State[3];
	Flow->SHA1[4] = SHA1State[4];

	// packet mode then print record as a packet 
	if (s_IsJSONPacket)
	{
		FlowDump(FileOut, DeviceName, CaptureName, PacketTS, Flow);
	}

	// update the flow records
	if (s_IsJSONFlow)
	{
		// insert to flow table
		FlowInsert(Flow, SHA1State, PktHeader->LengthWire, PacketTS);

		// purge the flow records every 100msec
		static u64 LastPacketTS = 0;
		s64 dTS = PacketTS - LastPacketTS;
		if (dTS > 1e9)
		{
			LastPacketTS = PacketTS;
			for (int i=0; i < s_FlowCnt; i++)
			{
				FlowRecord_t* Flow = &s_FlowList[i];	

				FlowDump(FileOut, DeviceName, CaptureName, PacketTS, Flow);
			}

			// reset index and counts
			FlowReset();
		}
	}
}

//---------------------------------------------------------------------------------------------

static void help(void)
{
	fprintf(stderr, "fmad engineering all rights reserved\n");
	fprintf(stderr, "http://www.fmad.io\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "pcap2json is a high speed PCAP meta data extraction utility\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "example converting a pcap to json:\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "cat /tmp/test.pcap | pcap2json > test.json\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Command Line Arguments:\n");
	fprintf(stderr, " --capture-name <name>  : capture name to use for ES Index data\n");
	fprintf(stderr, " --json-packet          : write JSON packet data\n");
	fprintf(stderr, " --json-flow            : write JSON flow data\n");
	fprintf(stderr, "\n");
}

//---------------------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
	u8* FileInName 		= NULL;
	u8* FileOutName 	= NULL;

	// get the hosts name
	u8 DeviceName[128];
	gethostname(DeviceName, sizeof(DeviceName));	

	u8 ClockStr[128];
	clock_str(ClockStr, clock_date() );

	u8 CaptureName[256];
	sprintf(CaptureName, "%s_%s", DeviceName, ClockStr); 

	for (int i=0; i < argc; i++)
	{
		if (strcmp(argv[i], "-v") == 0)
		{
			g_Verbose = true;
		}

		// output json packet data 
		if (strcmp(argv[i], "--json-packet") == 0)
		{
			fprintf(stderr, "Write JSON Packet meta data\n");
			s_IsJSONPacket = true;	
		}
		// output json flow data 
		if (strcmp(argv[i], "--json-flow") == 0)
		{
			fprintf(stderr, "Write JSON Flow meta data\n");
			s_IsJSONFlow = true;	
		}

		// capture name 
		if (strcmp(argv[i], "--capture-name") == 0)
		{
			strncpy(CaptureName, argv[i+1], sizeof(CaptureName));	
			fprintf(stderr, "Capture Name[%s]\n", CaptureName);
		}
		if (strcmp(argv[i], "--help") == 0)
		{
			help();
			return 0;
		}
	}

	CycleCalibration();

	FILE* FileIn 	= stdin;
	FILE* FileOut 	= stdout;

	u64  PCAPOffset	= 0;

	// read header
	PCAPHeader_t HeaderMaster;
	int rlen = fread(&HeaderMaster, 1, sizeof(HeaderMaster), FileIn);
	if (rlen != sizeof(HeaderMaster))
	{
		fprintf(stderr, "Failed to read pcap header\n");
		return 0;
	}
	PCAPOffset		= sizeof(PCAPHeader_t);

	u64 TScale = 0;
	switch (HeaderMaster.Magic)
	{
	case PCAPHEADER_MAGIC_NANO: fprintf(stderr, "PCAP Nano\n"); TScale = 1;    break;
	case PCAPHEADER_MAGIC_USEC: fprintf(stderr, "PCAP Micro\n"); TScale = 1000; break;
	}

	u64 NextPrintTS				= 0;

	u8* 			Pkt			= malloc(1024*1024);	
	PCAPPacket_t*	PktHeader	= (PCAPPacket_t*)Pkt;

	u64				PrintNextTSC	= 0;
	u64				StartTSC		= rdtsc();
	u64				LastTSC			= rdtsc();
	u64				PCAPOffsetLast	= 0;
	u64 			LastTS			= 0;

	// allocate and clear flow index
	s_FlowHash = (FlowRecord_t **)malloc( sizeof(FlowRecord_t *) * (2 << 20) );
	assert(s_FlowHash != NULL);

	// allocate statically allocated flow list
	s_FlowList = (FlowRecord_t *)malloc (sizeof(FlowRecord_t) * s_FlowMax );
	assert(s_FlowList != NULL);

	// reset flow info
	FlowReset();

	while (!feof(FileIn))
	{
		u64 TSC = rdtsc();

		// progress stats
		if (TSC > PrintNextTSC)
		{
			PrintNextTSC = TSC + ns2tsc(1e9);
			float bps = ((PCAPOffset - PCAPOffsetLast) * 8.0) / (tsc2ns(TSC - LastTSC)/1e9); 

			fprintf(stderr, "%.3f GB   %.6f Gbps\n", (float)PCAPOffset / kGB(1), bps / 1e9);

			LastTSC 		= TSC;
			PCAPOffsetLast 	= PCAPOffset;	
		}

		// header 
		int rlen = fread(PktHeader, 1, sizeof(PCAPPacket_t), FileIn);
		if (rlen != sizeof(PCAPPacket_t)) break;
		PCAPOffset += sizeof(PCAPPacket_t);

		// validate size
		if ((PktHeader->LengthCapture == 0) || (PktHeader->LengthCapture > 128*1024)) 
		{
			fprintf(stderr, "Invalid packet length: %i\n", PktHeader->LengthCapture);
			break;
		}

		// payload
		rlen = fread(PktHeader + 1, 1, PktHeader->LengthCapture, FileIn);
		if (rlen != PktHeader->LengthCapture)
		{
			fprintf(stderr, "payload read fail %i expect %i\n", rlen, PktHeader->LengthCapture);
			break;
		}
		PCAPOffset += PktHeader->LengthCapture; 

		u64 PacketTS = (u64)PktHeader->Sec * 1000000000ULL + (u64)PktHeader->NSec * TScale;

		// process each packet 
		DecodePacket(FileOut, DeviceName, CaptureName, PacketTS, PktHeader);

		LastTS = PacketTS;
	}

	// output last flow data
	if (s_IsJSONFlow)
	{
		for (int i=0; i < s_FlowCnt; i++)
		{
			FlowRecord_t* Flow = &s_FlowList[i];	
			FlowDump(FileOut, DeviceName, CaptureName, LastTS, Flow);
		}
		printf("Total Flows: %i\n", s_FlowCnt);
	}
}

/* vim: set ts=4 sts=4 */
