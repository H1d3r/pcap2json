#ifndef _PCAP2JSON_OUTPUT_H__
#define _PCAP2JSON_OUTPUT_H__

struct Output_t;

struct Output_t* 	Output_Create		(bool IsNULL, bool IsSTDOUT, bool IsESOut, bool IsCompress, u32 Output_BufferCnt, u32 OutputLineFlush, u64 Output_TimeFlush, u64 Output_ByteFlush, s32* Output_CPUMap);
void 				Output_LineAdd		(struct Output_t* Out, u8* Buffer, u32 BufferLen);
void 				Output_Close		(struct Output_t* Out);
u64 				Output_TotalByteSent(struct Output_t* Out);
u64 				Output_TotalLine	(struct Output_t* Out);
u64 				Output_ESErrorCnt	(struct Output_t* Out);
u64 				Output_ESPushCnt	(struct Output_t* Out);
void 				Output_ESHostAdd	(struct Output_t* Out, u8* HostName, u32 HostPort);
float 				Output_Stats		(struct Output_t* Out, 
										 bool IsReset, 
										 float* pTop, 
										 float* pCompress, 
										 float* pSend, 
										 float* pRecv, 
										 u64* pTotalCycle,
										 u64* pPendingB);

#endif
