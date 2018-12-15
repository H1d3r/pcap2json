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
#include <malloc.h>
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

double TSC2Nano = 0;

typedef struct
{
	u8						HostName[256];		// ES Host name
	u32						HostPort;			// ES Port name

} ESHost_t;

//---------------------------------------------------------------------------------------------
// tunables
bool			g_Verbose			= false;				// verbose print mode
s32				g_CPUCore[2]		= {14, 12};					// which CPU to run the main flow logic on
s32				g_CPUFlow[16]		= { 19, 20, 21, 22};	// cpu mapping for flow threads
s32				g_CPUOutput[16]		= { 25, 26, 27, 28, 25, 26, 27, 28};	// cpu mappings for output threads 

bool			g_IsJSONPacket		= false;			// output JSON packet format
bool			g_IsJSONFlow		= false;			// output JSON flow format

s64				g_FlowSampleRate	= 100e6;			// default to flow sample rate of 100msec

bool			g_JSONEnb_MAC		= true;				// include the MAC address in JSON output
bool			g_JSONEnb_VLAN		= true;				// include the VLAN in JSON output
bool			g_JSONEnb_MPLS		= true;				// include the MPLS in JSON output
bool			g_JSONEnb_IPV4		= true;				// include the IPV4 in JSON output
bool			g_JSONEnb_UDP		= true;				// include the UDP in JSON output
bool			g_JSONEnb_TCP		= true;				// include the UDP in JSON output

bool			g_Output_NULL		= false;			// benchmarking mode output to /dev/null 
bool			g_Output_STDOUT		= true;				// by default output to stdout 
bool			g_Output_ESPush		= false;			// direct ES HTTP Push 
u32				g_Output_LineFlush 	= 100e3;			// by default flush every 100e3 lines
u64				g_Output_TimeFlush 	= 1e9;				// by default flush every 1sec of activity 
u64				g_Output_ByteFlush 	= kMB(1);			// maximum buffer size per output upload 

u32				g_ESHostCnt 		= 0;				// number of active ES Hosts
ESHost_t		g_ESHost[128];							// list fo ES Hosts to output to
bool			g_ESCompress		= false;			// elastic push enable compression 

u8 				g_CaptureName[256];						// name of the capture / index to push to
u8				g_DeviceName[128];						// name of the device this is sourced from

//---------------------------------------------------------------------------------------------

// internal ring
static volatile u32		s_PacketRingPut[128];			// write pointer 
static volatile u32		s_PacketRingGet[128];			// read pointer 
static u32				s_PacketRingMsk = 63;			// mask 
static u32				s_PacketRingMax = 64;			// total entry count 
static PCAPPacket_t*	s_PacketRing[1024];				// packet ring buffer


static u32				s_PacketSizeHistoBin = 32;		// max index (not in byte)
static u32				s_PacketSizeHistoMax = 1024;	// max index (not in byte)
static u32				s_PacketSizeHisto[1024];

static void* Push_Worker(void* _User);

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
	fprintf(stderr, " --capture-name <name>          : capture name to use for ES Index data\n");
	fprintf(stderr, " --verbose                      : verbose output\n");
	fprintf(stderr, " --config <confrig file>        : read from config file\n");
	fprintf(stderr, "\n");
	fprintf(stderr, " --cpu-core   <cpu no>          : cpu map for core thread\n"); 
	fprintf(stderr, " --cpu-flow   <cpu0.. cpu3>     : cpu map for flow threads\n"); 
	fprintf(stderr, " --cpu-output <cpu0 .. cpu3>    : cpu map for output threads\n"); 
	fprintf(stderr, "\n");
	fprintf(stderr, " --json-packet                  : write JSON packet data\n");
	fprintf(stderr, " --json-flow                    : write JSON flow data\n");
	fprintf(stderr, "\n");

	fprintf(stderr, "Output Mode\n");
	fprintf(stderr, " --output-stdout                : writes output to STDOUT\n");
	fprintf(stderr, " --output-espush                : writes output directly to ES HTTP POST \n");
	fprintf(stderr, " --output-byteflush <bytes>     : max number of bytes per output push\n");
	fprintf(stderr, " --output-lineflush <line cnt>  : number of lines before flushing output (default 100e3)\n");
	fprintf(stderr, " --output-timeflush  <time ns>  : maximum amount of time since last flush (default 1e9)\n");

	fprintf(stderr, "\n");
	fprintf(stderr, "Flow specific options\n");
	fprintf(stderr, " --flow-samplerate <nanos>      : scientific notation flow sample rate. default 100e6 (100msec)\n");

	fprintf(stderr, "\n");
	fprintf(stderr, "JSON Output Control (by default everything is enabled)\n");
	fprintf(stderr, " --disable-mac                  : disable JSON MAC output\n");
	fprintf(stderr, " --disable-vlan                 : disable JSON VLAN output\n");
	fprintf(stderr, " --disable-mpls                 : disable JSON MPLS output\n");
	fprintf(stderr, " --disable-ipv4                 : disable JSON IPv4 output\n");
	fprintf(stderr, " --disable-udp                  : disable JSON UDP output\n");
	fprintf(stderr, " --disable-tcp                  : disable JSON TCP output\n");
	fprintf(stderr, "\n");

	fprintf(stderr, "Elastic Stack options\n");
	fprintf(stderr, " --es-host <hostname:port>      : Sets the ES Hostname\n");
	fprintf(stderr, " --es-compress                  : enables gzip compressed POST\n");
}

//---------------------------------------------------------------------------------------------

static bool ParseCommandLine(u8* argv[])
{
	u32 cnt = 0;
	if (strcmp(argv[0], "-v") == 0)
	{
		g_Verbose = true;
		cnt	+= 1;
	}
	// CPU for main process to run on 
	if (strcmp(argv[0], "--cpu-core") == 0)
	{
		g_CPUCore[0] = atoi(argv[1]);
		fprintf(stderr, "  Core on CPU %i\n", g_CPUCore[0]);
		cnt	+= 2;
	}
	if (strcmp(argv[0], "--cpu-flow") == 0)
	{
		g_CPUFlow[0] = atoi(argv[1]);
		g_CPUFlow[1] = atoi(argv[2]);
		g_CPUFlow[2] = atoi(argv[3]);
		g_CPUFlow[3] = atoi(argv[4]);

		fprintf(stderr, "  Flow on CPU %i %i %i %i\n", g_CPUFlow[0], g_CPUFlow[1], g_CPUFlow[2], g_CPUFlow[3]);
		cnt	+= 4 + 1;
	}
	if (strcmp(argv[0], "--cpu-output") == 0)
	{
		g_CPUOutput[0] = atoi(argv[1]);
		g_CPUOutput[1] = atoi(argv[2]);
		g_CPUOutput[2] = atoi(argv[3]);
		g_CPUOutput[3] = atoi(argv[4]);
		fprintf(stderr, "  Output on CPU %i %i %i %i\n", 
							g_CPUOutput[0], g_CPUOutput[1], g_CPUOutput[2], g_CPUOutput[3] );
		cnt	+= 4 + 1;
	}
	// output json packet data 
	if (strcmp(argv[0], "--json-packet") == 0)
	{
		fprintf(stderr, "  Write JSON Packet meta data\n");
		g_IsJSONPacket = true;	
		cnt	+= 1;
	}
	// output json flow data 
	if (strcmp(argv[0], "--json-flow") == 0)
	{
		fprintf(stderr, "  Write JSON Flow meta data\n");
		g_IsJSONFlow = true;	
		cnt	+= 1;
	}
	// capture name 
	if (strcmp(argv[0], "--capture-name") == 0)
	{
		strncpy(g_CaptureName, argv[1], sizeof(g_CaptureName));	
		fprintf(stderr, "  Capture Name[%s]\n", g_CaptureName);
		cnt	+= 2;
	}
	// benchmarking write to /dev/null 
	if (strcmp(argv[0], "--output-null") == 0)
	{
		g_Output_NULL 	= true;
		g_Output_ESPush = false;
		fprintf(stderr, "  Output to NULL\n");
		cnt	+= 1;
	}
	// default output to stdout
	if (strcmp(argv[0], "--output-stdout") == 0)
	{
		g_Output_NULL 	= false;
		g_Output_STDOUT = true;
		g_Output_ESPush = false;
		fprintf(stderr, "  Output to STDOUT\n");
		cnt	+= 1;
	}
	if (strcmp(argv[0], "--output-espush") == 0)
	{
		g_Output_NULL 	= false;
		g_Output_STDOUT = false;
		g_Output_ESPush = true;
		fprintf(stderr, "  Output to ES HTTP Push\n");
		cnt	+= 1;
	}
	if (strcmp(argv[0], "--output-lineflush") == 0)
	{
		g_Output_LineFlush = atof(argv[1]);
		fprintf(stderr, "  Output Line Flush: %i\n", g_Output_LineFlush);
		cnt	+= 2;
	}
	if (strcmp(argv[0], "--output-timeflush") == 0)
	{
		g_Output_TimeFlush = atof(argv[1]);
		fprintf(stderr, "  Output Time Flush: %lli ns\n", g_Output_TimeFlush);
		cnt	+= 2;
	}
	if (strcmp(argv[0], "--output-byteflush") == 0)
	{
		g_Output_ByteFlush = atof(argv[1]);
		fprintf(stderr, "  Output Byte Flush: %lli ns\n", g_Output_ByteFlush);
		cnt	+= 2;
	}
	// JSON output format
	if (strcmp(argv[0], "--disable-mac") == 0)
	{
		g_JSONEnb_MAC = false;
		fprintf(stderr, "  Disable JSON MAC Output\n");
		cnt	+= 1;
	}
	if (strcmp(argv[0], "--disable-vlan") == 0)
	{
		g_JSONEnb_VLAN = false;
		fprintf(stderr, "  Disable JSON VLAN Output\n");
		cnt	+= 1;
	}
	if (strcmp(argv[0], "--disable-mpls") == 0)
	{
		g_JSONEnb_MPLS = false;
		fprintf(stderr, "  Disable JSON MPLS Output\n");
		cnt	+= 1;
	}
	if (strcmp(argv[0], "--disable-ipv4") == 0)
	{
		g_JSONEnb_IPV4 = false;
		fprintf(stderr, "  Disable JSON IPv4 Output\n");
		cnt	+= 1;
	}
	if (strcmp(argv[0], "--disable-udp") == 0)
	{
		g_JSONEnb_UDP = false;
		fprintf(stderr, "  Disable JSON UDP Output\n");
		cnt	+= 1;
	}
	if (strcmp(argv[0], "--disable-tcp") == 0)
	{
		g_JSONEnb_TCP = false;
		fprintf(stderr, "  Disable JSON TCP Output\n");
		cnt	+= 1;
	}

	// ES specific 
	if (strcmp(argv[0], "--es-host") == 0)
	{
		// split into host/port from <hostname:port>
		u32 StrPos = 0;
		u8 StrList[2][256];
		u32 Len = 0;

		u8* HostPort = argv[1];
		for (int j=0; j < strlen(HostPort); j++)
		{
			u32 c = HostPort[j];	
			if ((c == ':') || (c == 0))
			{
				StrList[StrPos][Len] = 0;
				StrPos++;
				Len = 0;
			}
			else
			{
				StrList[StrPos][Len++] = c;
			}
		}
		StrList[StrPos][Len] = 0;
		StrPos++;

		if (StrPos != 2)
		{
			fprintf(stderr, "  Format incorrect\n--es-host <hostname:port> found %i\n", StrPos);
			return false;
		}

		ESHost_t* Host = &g_ESHost[g_ESHostCnt++];

		strncpy(Host->HostName, StrList[0], sizeof(Host->HostName));
		Host->HostPort = atoi(StrList[1]);
		fprintf(stderr, "  ES[%i] HostName [%s]  HostPort:%i\n", g_ESHostCnt, Host->HostName, Host->HostPort);
		cnt	+= 2;
	}
	if (strcmp(argv[0], "--es-compress") == 0)
	{
		g_ESCompress = true;
		fprintf(stderr, "  ES Compression Enabled\n");
		cnt	+= 1;
	}

	// flow specific
	if (strcmp(argv[0], "--flow-samplerate") == 0)
	{
		g_FlowSampleRate = atof(argv[1]);
		fprintf(stderr, "  Flow Sample rate %.3f msec\n", g_FlowSampleRate / 1e6);
		cnt	+= 2;
	}

	// create a unique id so calling applications
	// can identify it with ps 
	if (strcmp(argv[0], "--uid") == 0)
	{
		u8* uid = argv[1];
		fprintf(stderr, "  UID [%s]\n", uid); 
		cnt	+= 2;
	}

	if (strcmp(argv[0], "--help") == 0)
	{
		help();
		return false;
	}

	// unknown command
	if (cnt == 0)
	{
		fprintf(stderr, "  Unknown command line option [%s]\n", argv[0]);
		return 0;
	}

	return cnt;
}

//---------------------------------------------------------------------------------------------
// read command line opts from file
static bool ParseConfigFile(u8* ConfigFile)
{
	fprintf(stderr, "Config File [%s]\n", ConfigFile);	

	FILE* F = fopen(ConfigFile, "r");
	if (!F)
	{
		fprintf(stderr, "unable to open config file [%s]\n", ConfigFile);
		return 0;
	}

	u32 LinePos = 0;
	u32 LineListPos = 0;
	u8* LineList[256];
	u8  LineBuffer[256];
	while (!feof(F))
	{
		u32 c = fgetc(F);
		switch (c)
		{
			case '\n':
			case ' ':
				{
					// remove any trailing whitespace
					// easy to copy the cmdline args + paste it into a config file
					for (int k=LinePos-1;  k > 0; k--)
					{
						if (LineBuffer[k] == ' ') LineBuffer[k] = 0;
						else break;
					}
					LineBuffer[LinePos++] = 0;		// asciiz

					if (LinePos > 1)
					{
						LineList[LineListPos] = strdup(LineBuffer);
						LineListPos += 1;
					}

					LinePos		=  0;
				}
				break;

			// argument encased in "" 
			case '"':
				{
					// consume line buffer until matching "
					while (!feof(F))
					{
						c = fgetc(F);
						if (c == '"') break;

						LineBuffer[LinePos++] = c;
					}
				}
				break;

			default:
				LineBuffer[LinePos++] = c;
				break;
		}
	}
	fclose(F);

	// parse each command
	for (int j=0; j < LineListPos; j++)
	{
		fprintf(stderr, "[%s]\n", LineList[j]);	
		if (LineList[j][0] == '#')
		{
			//fprintf(stderr, "   comment skipping\n");
			continue;
		}

		u32 inc = ParseCommandLine(&LineList[j]);
		if (inc == 0) return false;

		j += (inc - 1);
	}

	return true;
}

//---------------------------------------------------------------------------------------------
// dump performance stats
static void ProfileDump(struct Output_t* Out)
{
	fProfile_Dump(0);
	printf("\n");

	float OutputWorkerCPU;
	float OutputWorkerCPUCompress;
	float OutputWorkerCPUSend;
	float OutputWorkerCPURecv;
	u64   OutputTotalCycle;
	Output_Stats(Out, 1,  	&OutputWorkerCPU, 
							&OutputWorkerCPUCompress, 
							&OutputWorkerCPUSend, 
							&OutputWorkerCPURecv,
							&OutputTotalCycle);

	printf("Output Worker CPU\n");
	printf("  Top     : %.6f\n", OutputWorkerCPU);
	printf("  Compress: %.6f\n", OutputWorkerCPUCompress);
	printf("  Send    : %.6f\n", OutputWorkerCPUSend);
	printf("  Recv    : %.6f\n", OutputWorkerCPURecv);
	printf("  Total   : %.6f sec\n", tsc2ns(OutputTotalCycle)/1e9 );
	printf("\n");

	u64 FlowCntTotal = 0;
	float FlowCPUDecode = 0;
	Flow_Stats(true, NULL, &FlowCntTotal, &FlowCPUDecode);

	printf("Flow:\n");
	printf("  Total   : %lli\n", FlowCntTotal);
	printf("  CPU     : %.3f\n", FlowCPUDecode);
	printf("\n");

	// packet size histogram
	printf("Packet Size Histogram:\n");	

	u32 MaxCnt = 0;
	for (int i=0; i < s_PacketSizeHistoMax; i++)
	{
		MaxCnt = (MaxCnt < s_PacketSizeHisto[i]) ? s_PacketSizeHisto[i] : MaxCnt;
	}
	for (int i=0; i < s_PacketSizeHistoMax; i++)
	{
		u32 Size = i * s_PacketSizeHistoBin;
		if (s_PacketSizeHisto[i] == 0) continue;

		printf("%5i : %10i :", Size, s_PacketSizeHisto[i]);
		u32 Cnt = (s_PacketSizeHisto[i] * 80 ) / MaxCnt;
		for (int j=0; j < Cnt; j++) printf("*");

		printf("\n");
	}
	memset(s_PacketSizeHisto, 0, sizeof(s_PacketSizeHisto));
	printf("\n");

	fflush(stdout);
}

//---------------------------------------------------------------------------------------------

int main(int argc, u8* argv[])
{
	// get the hosts name
	gethostname(g_DeviceName, sizeof(g_DeviceName));	

	u8 ClockStr[128];
	clock_str(ClockStr, clock_date() );

	sprintf(g_CaptureName, "%s_%s", g_DeviceName, ClockStr); 
	for (int i=1; i < argc; i++)
	{
		// config file was specified 
		if (strcmp(argv[i], "--config") == 0)
		{
			if (!ParseConfigFile(argv[i+1])) return 0;
			i += 1;
		}
		else
		{
			fprintf(stderr, "[%s]\n", argv[i]); 

			u32 inc = ParseCommandLine(&argv[i]);
			if (inc == 0) return 0;

			i += (inc - 1);
		}
	}

	u64 TS0 = clock_ns();

	// print cpu mapping
	fprintf(stderr, "CPU Mapping\n");
	fprintf(stderr, "  Core   %i %i\n", g_CPUCore[0], g_CPUCore[1]);
	fprintf(stderr, "  Flow   %i %i %i %i\n", g_CPUFlow[0], g_CPUFlow[1], g_CPUFlow[2], g_CPUFlow[3]);
	fprintf(stderr, "  Output %i %i %i %i\n",
							g_CPUOutput[0], g_CPUOutput[1], g_CPUOutput[2], g_CPUOutput[3]);

	// set cpu affinity
	if (g_CPUCore[0] >= 0) 
	{
		cpu_set_t Thread0CPU;
		CPU_ZERO(&Thread0CPU);
		CPU_SET (g_CPUCore[0], &Thread0CPU);
		pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &Thread0CPU);
	}
	CycleCalibration();

	FILE* FileIn 	= stdin;
	FILE* FileOut 	= stdout;


	// read header
	PCAPHeader_t HeaderMaster;
	int rlen = fread(&HeaderMaster, 1, sizeof(HeaderMaster), FileIn);
	if (rlen != sizeof(HeaderMaster))
	{
		fprintf(stderr, "Failed to read pcap header\n");
		return 0;
	}

	u64 TScale = 0;
	switch (HeaderMaster.Magic)
	{
	case PCAPHEADER_MAGIC_NANO: fprintf(stderr, "PCAP Nano\n"); TScale = 1;    break;
	case PCAPHEADER_MAGIC_USEC: fprintf(stderr, "PCAP Micro\n"); TScale = 1000; break;
	}

	u64 NextPrintTS				= 0;


	u64				PrintNextTSC	= 0;
	u64				ProfileNextTSC	= 0;
	u64				StartTSC		= rdtsc();
	u64				LastTSC			= rdtsc();
	u64 			LastTS			= 0;

	u64				TotalByte		= 0;
	u64				TotalPkt		= 0;

	// output + add all the ES targets
	struct Output_t* Out = Output_Create(	g_Output_NULL,
											g_Output_STDOUT, 
											g_Output_ESPush, 
											g_ESCompress, 
											g_Output_LineFlush,
											g_Output_TimeFlush,
											g_Output_ByteFlush,
											g_CPUOutput); 
	for (int i=0; i < g_ESHostCnt; i++)
	{
		Output_ESHostAdd(Out, g_ESHost[i].HostName, g_ESHost[i].HostPort);
	}

	// init flow state
	Flow_Open(Out, g_CPUFlow);

	// allocate packet ring
	s_PacketRingPut[0] = 0;
	s_PacketRingGet[0] = 0;
	for (int i=0; i < s_PacketRingMax; i++)
	{
		s_PacketRing[i] = (PCAPPacket_t*)memalign(4096, 16*1024);
	}

	// create worker thread to push the raw ring data 
	pthread_t PushThread;
	pthread_create(&PushThread, NULL, Push_Worker, (void*)Out);

	cpu_set_t Thread1CPU;
	CPU_ZERO(&Thread1CPU);
	CPU_SET (g_CPUCore[1], &Thread1CPU);
	pthread_setaffinity_np(PushThread, sizeof(cpu_set_t), &Thread1CPU);

	u64 PacketTSFirst = 0;
	u64 PacketTSLast  = 0;
	u64 TotalByteLast = 0;
	u64 TotalPktLast = 0;

	u64 OutputLineLast = 0;

	u64 PacketTSLastSample = 0;
	u64 DecodeTimeLast = 0;
	u64 DecodeTimeTSC = 0;

	while (!feof(FileIn))
	{
		u64 TSC = rdtsc();

		// progress stats
		if (TSC > PrintNextTSC)
		{
			PrintNextTSC = TSC + ns2tsc(1e9);

			u64 OutputByte = Output_TotalByteSent(Out);
			u64 OutputLine = Output_TotalLine(Out);	

			float bps = ((TotalByte - TotalByteLast) * 8.0) / (tsc2ns(TSC - LastTSC)/1e9); 
			float lps = (OutputLine - OutputLineLast) / (tsc2ns(TSC - LastTSC)/1e9); 
			float pps = (TotalPkt - TotalPktLast) / (tsc2ns(TSC - LastTSC)/1e9); 


			// is it keeping up ? > 1.0 means it will lag
			float PCAPWallTime 	= (PacketTSLast - PacketTSFirst) / 1e9;
			float DecodeTime 	= tsc2ns(DecodeTimeTSC) / 1e9; 

			// decode rate since last print
			float SamplePCAPWallTime 	= (PacketTSLast - PacketTSLastSample) / 1e9;
			float SampleDecodeTime 		= (DecodeTime - DecodeTimeLast) / 1e9; 

			float PCAPbps = ((TotalByte - TotalByteLast) * 8.0) / SamplePCAPWallTime; 

			float OutputWorkerCPU;
			float OutputWorkerCPURecv;
			Output_Stats(Out, 0,  &OutputWorkerCPU, NULL, NULL, &OutputWorkerCPURecv, NULL);

			u32 FlowCntSnapshot;	
			float FlowCPU;
			Flow_Stats(false, &FlowCntSnapshot, NULL, &FlowCPU);

			fprintf(stderr, "[%s] In:%.3f GB %.2f Mpps %.2f Gbps PCAP: %6.2f Gbps | Out %.5f GB Flows/Snap: %6i FlowCPU:%.2f | ESPush:%6lli %6.2fK ESErr %4lli | OutCPU: %.2f (%.2f)\n", 

								FormatTS(PacketTSLast),

								(float)TotalByte / kGB(1), 
								pps / 1e6, 
								bps / 1e9, 
								PCAPbps / 1e9, 
								OutputByte / 1e9, 
								FlowCntSnapshot, 
								FlowCPU,
								Output_ESPushCnt(Out),
								lps/1e3,
								Output_ESErrorCnt(Out),
								OutputWorkerCPU,
								OutputWorkerCPURecv
							);
			fflush(stderr);

			LastTSC 			= TSC;
			TotalByteLast		= TotalByte;
			TotalPktLast		= TotalPkt;
		
			OutputLineLast		= OutputLine;

			PacketTSLastSample 	= PacketTSLast;
			DecodeTimeLast 		= DecodeTime;
		}

		// dump performance stats every 1min
		if (TSC > ProfileNextTSC)
		{
			ProfileNextTSC = TSC + ns2tsc(60e9);
			ProfileDump(Out);
		}

		fProfile_Start(0, "Top");

		fProfile_Start(7, "PacketStall");

		//PacketBuffer_t*	Pkt			= Flow_PacketAlloc();
		///PCAPPacket_t*	PktHeader	= (PCAPPacket_t*)Pkt->Buffer;

		while ( ((s_PacketRingPut[0] + 4) & s_PacketRingMsk) == (s_PacketRingGet[0] & s_PacketRingMsk))
		{
			ndelay(100);
			//usleep(0);
		}
		PCAPPacket_t*	PktHeader	= (PCAPPacket_t*)s_PacketRing[ s_PacketRingPut[0] & s_PacketRingMsk ];

		fProfile_Stop(7);

		fProfile_Start(6, "PacketFetch");

		// header
		int rlen = fread(PktHeader, 1, sizeof(PCAPPacket_t), FileIn);
		if (rlen != sizeof(PCAPPacket_t)) break;

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
		u64 TS = (u64)PktHeader->Sec * 1000000000ULL + (u64)PktHeader->NSec * TScale;

		if (PacketTSFirst == 0) PacketTSFirst = TS;
		PacketTSLast = TS;

		// update size histo
		u32 SizeIndex = (PktHeader->LengthWire / s_PacketSizeHistoBin);
		if (SizeIndex >= s_PacketSizeHistoMax) SizeIndex = s_PacketSizeHistoMax - 1;
		s_PacketSizeHisto[SizeIndex]++;


		s_PacketRingPut[0]++;

		fProfile_Stop(6);
		u64 TSC0 		= rdtsc();

		// queue the packet for processing 
		//Flow_PacketQueue(Pkt);

		DecodeTimeTSC 	+= rdtsc() -  TSC0;
		LastTS 			= TS;

		TotalByte		+= PktHeader->LengthWire;
		TotalPkt		+= 1;

		fProfile_Stop(0);
	}
	fProfile_Stop(6);
	fProfile_Stop(0);

	// flush any remaining flows
	Flow_Close(Out, LastTS);

	ProfileDump(Out);

	// final stats

	u64 TS1 = clock_ns();
	float dT = (TS1 - TS0) / 1e9;

	float bps = (TotalByte * 8.0) / dT;
	float pps = (TotalPkt * 8.0) / dT;

	float obps = (Output_TotalByteSent(Out) * 8.0) / dT;

	u64 TotalLine = Output_TotalLine(Out);	
	float lps = TotalLine / dT;

	float PCAPWallTime = (PacketTSLast - PacketTSFirst) / 1e9;
	printf("PCAPWall time: %.2f sec ProcessTime %.2f sec (%.3f)\n", PCAPWallTime, dT, dT / PCAPWallTime);

	printf("Total Time: %.2f sec RawInput[%.3f Gbps %.f Pps] Output[%.3f Gbps] TotalLine:%lli %.f Line/Sec\n", dT, bps / 1e9, pps, obps / 1e9, TotalLine, lps); 
}

//---------------------------------------------------------------------------------------------

static void* Push_Worker(void* _User)
{
	printf("Push Worker Started\n");
	while (true)
	{
		if (s_PacketRingPut[0] == s_PacketRingGet[0])
		{
			ndelay(100);
			//usleep(0);
		}
		else
		{
			PCAPPacket_t* Packet = s_PacketRing[ s_PacketRingGet[0] & s_PacketRingMsk ];

			PacketBuffer_t*	Pkt			= Flow_PacketAlloc();
			PCAPPacket_t*	PktHeader	= (PCAPPacket_t*)Pkt->Buffer;

			memcpy(PktHeader, Packet, sizeof(PCAPPacket_t) + Packet->LengthCapture);

			Pkt->TS = (u64)PktHeader->Sec * 1000000000ULL + (u64)PktHeader->NSec;

			Flow_PacketQueue(Pkt);

			s_PacketRingGet[0]++;
		}
	}
}

/* vim: set ts=4 sts=4 */
