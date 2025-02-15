#ifndef _pioscsidt_h_
#define _pioscsidt_h_

// PioScsiExtendedInfoGuid - PioScsiExtendedInfo
// PhyzIO SCSI Extended Information
#define PioScsiWmi_ExtendedInfo_Guid \
    { 0x5cdac4f6,0x3d46,0x44e2, { 0x8d,0xee,0x01,0x60,0x6e,0x11,0xe2,0x65 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(PioScsiExtendedInfoGuid_GUID, \
            0x5cdac4f6,0x3d46,0x44e2,0x8d,0xee,0x01,0x60,0x6e,0x11,0xe2,0x65);
#endif


typedef struct _PioScsiExtendedInfo
{
    // 
    ULONG QueueDepth;
    #define PioScsiExtendedInfo_QueueDepth_SIZE sizeof(ULONG)
    #define PioScsiExtendedInfo_QueueDepth_ID 1

    // 
    UCHAR QueuesCount;
    #define PioScsiExtendedInfo_QueuesCount_SIZE sizeof(UCHAR)
    #define PioScsiExtendedInfo_QueuesCount_ID 2

    // 
    BOOLEAN Indirect;
    #define PioScsiExtendedInfo_Indirect_SIZE sizeof(BOOLEAN)
    #define PioScsiExtendedInfo_Indirect_ID 3

    // 
    BOOLEAN EventIndex;
    #define PioScsiExtendedInfo_EventIndex_SIZE sizeof(BOOLEAN)
    #define PioScsiExtendedInfo_EventIndex_ID 4

    // 
    BOOLEAN DpcRedirection;
    #define PioScsiExtendedInfo_DpcRedirection_SIZE sizeof(BOOLEAN)
    #define PioScsiExtendedInfo_DpcRedirection_ID 5

    // 
    BOOLEAN ConcurrentChannels;
    #define PioScsiExtendedInfo_ConcurrentChannels_SIZE sizeof(BOOLEAN)
    #define PioScsiExtendedInfo_ConcurrentChannels_ID 6

    // 
    BOOLEAN InterruptMsgRanges;
    #define PioScsiExtendedInfo_InterruptMsgRanges_SIZE sizeof(BOOLEAN)
    #define PioScsiExtendedInfo_InterruptMsgRanges_ID 7

    // 
    BOOLEAN CompletionDuringStartIo;
    #define PioScsiExtendedInfo_CompletionDuringStartIo_SIZE sizeof(BOOLEAN)
    #define PioScsiExtendedInfo_CompletionDuringStartIo_ID 8

    // 
    BOOLEAN RingPacked;
    #define PioScsiExtendedInfo_RingPacked_SIZE sizeof(BOOLEAN)
    #define PioScsiExtendedInfo_RingPacked_ID 9

    // 
    ULONG PhysicalBreaks;
    #define PioScsiExtendedInfo_PhysicalBreaks_SIZE sizeof(ULONG)
    #define PioScsiExtendedInfo_PhysicalBreaks_ID 10

} PioScsiExtendedInfo, *PPioScsiExtendedInfo;

#define PioScsiExtendedInfo_SIZE (FIELD_OFFSET(PioScsiExtendedInfo, PhysicalBreaks) + PioScsiExtendedInfo_PhysicalBreaks_SIZE)

#endif
