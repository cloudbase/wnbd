/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    srbhelper.h

Abstract:

    This file defines helpful inline functions, accessors, and wrappers relevant
    SCSI_REQUEST_BLOCK and STORAGE_REQUEST_BLOCK structures.

--*/

// TODO: just include srbhelper.h and drop redundant definitions.

#ifndef _NTSRBHELPER_
#define _NTSRBHELPER_

#if !defined(_NTSTORPORT_) && !defined(_NTSRB_)
#include <srb.h>
#include <scsi.h>
#endif

#if (NTDDI_VERSION >= NTDDI_WIN8)

//
// If you do not wish to use NT_ASSERT, put "#define SRBHELPER_ASSERT ASSERT"
// before including this file.
//
#if !defined(SRBHELPER_ASSERT)
#define SRBHELPER_ASSERT NT_ASSERT
#endif

#if !defined(SRB_ALIGN_SIZEOF)
#define SRB_ALIGN_SIZEOF(x) (((ULONG_PTR)(sizeof(x) + sizeof(PVOID) - 1)) & ~(sizeof(PVOID) - 1))
#endif

#if defined(_NTSTORPORT_) 
#define SrbMoveMemory(Destination, Source, Length) StorPortMoveMemory(Destination, Source, Length) 
#elif defined(_NTDDK_)
#define SrbMoveMemory(Destination, Source, Length) RtlMoveMemory(Destination, Source, Length)  
#else
#define SrbMoveMemory(Destination, Source, Length) memmove(Destination, Source, Length)  
#endif

#if defined(_NTDDK_)
#define SrbCopyMemory(Destination, Source, Length) RtlCopyMemory(Destination, Source, Length)  
#else
#define SrbCopyMemory(Destination, Source, Length) memcpy(Destination, Source, Length)  
#endif

#if defined(_NTDDK_)
#define SrbZeroMemory(Destination, Length) RtlZeroMemory(Destination, Length)   
#else
#define SrbZeroMemory(Destination, Length) memset(Destination, 0, Length)          
#endif     

#if defined(_NTDDK_)
#define SrbEqualMemory(Source1, Source2, Length) RtlEqualMemory(Source1, Source2, Length)   
#else
#define SrbEqualMemory(Source1, Source2, Length) (memcmp(Source1, Source2, Length) == 0)       
#endif 

FORCEINLINE PSRBEX_DATA
SrbGetSrbExDataByIndex(
    _In_ PSTORAGE_REQUEST_BLOCK Srb,
    _In_ ULONG SrbExDataIndex
)
{
    PSRBEX_DATA srbExData = NULL;

    if ((Srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK) &&
        (SrbExDataIndex < Srb->NumSrbExData) &&
        (Srb->SrbExDataOffset[SrbExDataIndex]) &&
        (Srb->SrbExDataOffset[SrbExDataIndex] >= sizeof(STORAGE_REQUEST_BLOCK)) &&
        (Srb->SrbExDataOffset[SrbExDataIndex] < Srb->SrbLength))
    {
        srbExData = (PSRBEX_DATA)((PUCHAR)Srb + Srb->SrbExDataOffset[SrbExDataIndex]);
    }

    return srbExData;
}

FORCEINLINE PSRBEX_DATA
SrbGetSrbExDataByType(
    _In_ PSTORAGE_REQUEST_BLOCK Srb,
    _In_ SRBEXDATATYPE Type
)
{
    if ((Srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK) &&
        (Srb->NumSrbExData > 0))
    {
        PSRBEX_DATA srbExData = NULL;
        UCHAR i = 0;

        for (i = 0; i < Srb->NumSrbExData; i++)
        {
            if (Srb->SrbExDataOffset[i] >= sizeof(STORAGE_REQUEST_BLOCK) &&
                Srb->SrbExDataOffset[i] < Srb->SrbLength)
            {
                srbExData = (PSRBEX_DATA)((PUCHAR)Srb + Srb->SrbExDataOffset[i]);
                if (srbExData->Type == Type)
                {
                    return srbExData;
                }
            }
        }
    }

    return NULL;
}

FORCEINLINE PSRBEX_DATA
SrbGetPrimarySrbExData(
    _In_ PSTORAGE_REQUEST_BLOCK Srb
)
{
    if (Srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        switch (Srb->SrbFunction)
        {
        case SRB_FUNCTION_POWER:
            return SrbGetSrbExDataByType(Srb, SrbExDataTypePower);

        case SRB_FUNCTION_PNP:
            return SrbGetSrbExDataByType(Srb, SrbExDataTypePnP);

        case SRB_FUNCTION_WMI:
            return SrbGetSrbExDataByType(Srb, SrbExDataTypeWmi);

        case SRB_FUNCTION_EXECUTE_SCSI:
        {
            PSRBEX_DATA srbExData = NULL;
            UCHAR i = 0;

            for (i = 0; i < Srb->NumSrbExData; i++)
            {
                if (Srb->SrbExDataOffset[i] >= sizeof(STORAGE_REQUEST_BLOCK) &&
                    Srb->SrbExDataOffset[i] < Srb->SrbLength)
                {
                    srbExData = (PSRBEX_DATA)((PUCHAR)Srb + Srb->SrbExDataOffset[i]);
                    if (srbExData->Type == SrbExDataTypeScsiCdb16 ||
                        srbExData->Type == SrbExDataTypeScsiCdb32 ||
                        srbExData->Type == SrbExDataTypeScsiCdbVar)
                    {
                        return srbExData;
                    }
                }
            }
            return NULL;
        }

        default:
            return NULL;
        }
    }

    return NULL;
}

FORCEINLINE PSTOR_ADDRESS
SrbGetAddress(
    _In_ PSTORAGE_REQUEST_BLOCK Srb
)
{
    PSTOR_ADDRESS storAddr = NULL;

    if (Srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        SRBHELPER_ASSERT(Srb->AddressOffset);

        if (Srb->AddressOffset)
        {
            storAddr = (PSTOR_ADDRESS)((PUCHAR)Srb + Srb->AddressOffset);

            //
            // We currently only support BTL8, so assert if the type is something
            // different.
            //
            SRBHELPER_ASSERT(storAddr->Type == STOR_ADDRESS_TYPE_BTL8);
        }
    }

    return storAddr;
}

FORCEINLINE BOOLEAN
SrbCopySrb(
    _In_ PVOID DestinationSrb,
    _In_ ULONG DestinationSrbLength,
    _In_ PVOID SourceSrb
)
/*

Description:
    This function copies the given source SRB to the destination SRB. The caller
    must ensure the destination SRB points to a large enough memory block to
    hold the source SRB.

Arguments:
    DestinationSrb - A pointer to either a STORAGE_REQUEST_BLOCK or a
        SCSI_REQUEST_BLOCK which will accept the copied data.
    DestinationSrbLength - The size of the buffer DestinationSrb points to.
    SourceSrb - A pointer to either a STORAGE_REQUEST_BLOCK or a
        SCSI_REQUEST_BLOCK which is the source of the copied data.

Returns:
    TRUE - If the copy succeeded.
    FALSE - If the copy failed.

*/
{
    PSTORAGE_REQUEST_BLOCK sourceSrb = (PSTORAGE_REQUEST_BLOCK)SourceSrb;
    BOOLEAN status = FALSE;

    if (sourceSrb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        if (DestinationSrbLength >= sourceSrb->SrbLength)
        {
            SrbCopyMemory(DestinationSrb, SourceSrb, sourceSrb->SrbLength);
            status = TRUE;
        }
    }
    else
    {
        if (DestinationSrbLength >= SCSI_REQUEST_BLOCK_SIZE)
        {
            SrbCopyMemory(DestinationSrb, SourceSrb, SCSI_REQUEST_BLOCK_SIZE);
            status = TRUE;
        }
    }

    return status;
}

FORCEINLINE VOID
SrbZeroSrb(
    _In_ PVOID Srb
)
/*

Description:
    This function zeroes out the given SRB, but preserves the Function and
    Length fields.  If it is a STORAGE_REQUEST_BLOCK, the SrbLength field is
    also preserved.

Arguments:
    Srb - A pointer to either the STORAGE_REQUEST_BLOCK or SCSI_REQUEST_BLOCK
          to be zeroed.
*/
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    UCHAR function = srb->Function;
    USHORT length = srb->Length;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        ULONG srbLength = srb->SrbLength;

        SrbZeroMemory(Srb, srb->SrbLength);

        srb->SrbLength = srbLength;
    }
    else
    {
        SrbZeroMemory(Srb, sizeof(SCSI_REQUEST_BLOCK));
    }

    srb->Function = function;
    srb->Length = length;
}

FORCEINLINE ULONG
SrbGetSrbLength(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        return srb->SrbLength;
    }
    else
    {
        return sizeof(SCSI_REQUEST_BLOCK);
    }
}

FORCEINLINE VOID
SrbSetSrbLength(
    _In_ PVOID Srb,
    _In_ ULONG Length
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->SrbLength = Length;
    }
    else
    {
        //
        // No need to do anything here since SCSI_REQUEST_BLOCKs have a static
        // size.
        //
    }
}

FORCEINLINE ULONG
SrbGetDefaultSrbLengthFromFunction(
    _In_ ULONG SrbFunction
)
{
    switch (SrbFunction)
    {
    case SRB_FUNCTION_PNP:
        return SRB_ALIGN_SIZEOF(STORAGE_REQUEST_BLOCK)
            + SRB_ALIGN_SIZEOF(STOR_ADDR_BTL8)
            + SRB_ALIGN_SIZEOF(SRBEX_DATA_PNP);
    case SRB_FUNCTION_POWER:
        return SRB_ALIGN_SIZEOF(STORAGE_REQUEST_BLOCK)
            + SRB_ALIGN_SIZEOF(STOR_ADDR_BTL8)
            + SRB_ALIGN_SIZEOF(SRBEX_DATA_POWER);
    case SRB_FUNCTION_WMI:
        return SRB_ALIGN_SIZEOF(STORAGE_REQUEST_BLOCK)
            + SRB_ALIGN_SIZEOF(STOR_ADDR_BTL8)
            + SRB_ALIGN_SIZEOF(SRBEX_DATA_WMI);
    case SRB_FUNCTION_EXECUTE_SCSI:
        return SRB_ALIGN_SIZEOF(STORAGE_REQUEST_BLOCK)
            + SRB_ALIGN_SIZEOF(STOR_ADDR_BTL8)
            + SRB_ALIGN_SIZEOF(SRBEX_DATA_SCSI_CDB16);
    case SRB_FUNCTION_IO_CONTROL:
        return SRB_ALIGN_SIZEOF(STORAGE_REQUEST_BLOCK)
            + SRB_ALIGN_SIZEOF(STOR_ADDR_BTL8);
    default:
        return SRB_ALIGN_SIZEOF(STORAGE_REQUEST_BLOCK)
            + SRB_ALIGN_SIZEOF(STOR_ADDR_BTL8);
    }
}


FORCEINLINE PCDB
SrbGetScsiData(
    _In_ PSTORAGE_REQUEST_BLOCK SrbEx,
    _In_opt_ PUCHAR CdbLength8,
    _In_opt_ PULONG CdbLength32,
    _In_opt_ PUCHAR ScsiStatus,
    _In_opt_ PVOID* SenseInfoBuffer,
    _In_opt_ PUCHAR SenseInfoBufferLength
)
/*++

Routine Description:

    Helper function to retrieve SCSI related fields from an extended SRB. If SRB is
    not a SRB_FUNCTION_EXECUTE_SCSI or not an extended SRB, default values will be returned.

Arguments:

    SrbEx - Pointer to extended SRB.

    CdbLength8 - Pointer to buffer to hold CdbLength field value for
                 SRBEX_DATA_SCSI_CDB16 or SRBEX_DATA_SCSI_CDB32

    CdbLength32 - Pointer to buffer to hold CdbLength field value for
                  SRBEX_DATA_SCSI_CDB_VAR

    ScsiStatus - Pointer to buffer to hold ScsiStatus field value.

    SenseInfoBuffer - Pointer to buffer to hold SenseInfoBuffer value.

    SenseInfoBufferLength - Pointer to buffer to hold SenseInfoBufferLength value.

Return Value:

    Pointer to Cdb field or NULL if SRB is not a SRB_FUNCTION_EXECUTE_SCSI.

--*/
{
    PCDB Cdb = NULL;
    ULONG i;
    PSRBEX_DATA SrbExData = NULL;
    BOOLEAN FoundEntry = FALSE;

    if ((SrbEx->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK) &&
        (SrbEx->SrbFunction == SRB_FUNCTION_EXECUTE_SCSI)) {
        SRBHELPER_ASSERT(SrbEx->NumSrbExData > 0);

        for (i = 0; i < SrbEx->NumSrbExData; i++) {

            // Skip any invalid offsets
            if ((SrbEx->SrbExDataOffset[i] < sizeof(STORAGE_REQUEST_BLOCK)) ||
                (SrbEx->SrbExDataOffset[i] >= SrbEx->SrbLength)) {
                // Catch any invalid offsets
                SRBHELPER_ASSERT(FALSE);
                continue;
            }

            SrbExData = (PSRBEX_DATA)((PUCHAR)SrbEx + SrbEx->SrbExDataOffset[i]);

            switch (SrbExData->Type) {

            case SrbExDataTypeScsiCdb16:
                if (SrbEx->SrbExDataOffset[i] + sizeof(SRBEX_DATA_SCSI_CDB16) <= SrbEx->SrbLength) {
                    FoundEntry = TRUE;
                    if (CdbLength8) {
                        *CdbLength8 = ((PSRBEX_DATA_SCSI_CDB16)SrbExData)->CdbLength;
                    }

                    if (((PSRBEX_DATA_SCSI_CDB16)SrbExData)->CdbLength > 0) {
                        Cdb = (PCDB)((PSRBEX_DATA_SCSI_CDB16)SrbExData)->Cdb;
                    }

                    if (ScsiStatus) {
                        *ScsiStatus =
                            ((PSRBEX_DATA_SCSI_CDB16)SrbExData)->ScsiStatus;
                    }

                    if (SenseInfoBuffer) {
                        *SenseInfoBuffer =
                            ((PSRBEX_DATA_SCSI_CDB16)SrbExData)->SenseInfoBuffer;
                    }

                    if (SenseInfoBufferLength) {
                        *SenseInfoBufferLength =
                            ((PSRBEX_DATA_SCSI_CDB16)SrbExData)->SenseInfoBufferLength;
                    }

                }
                else {
                    // Catch invalid offset
                    SRBHELPER_ASSERT(FALSE);
                }
                break;

            case SrbExDataTypeScsiCdb32:
                if (SrbEx->SrbExDataOffset[i] + sizeof(SRBEX_DATA_SCSI_CDB32) <= SrbEx->SrbLength) {
                    FoundEntry = TRUE;
                    if (CdbLength8) {
                        *CdbLength8 = ((PSRBEX_DATA_SCSI_CDB32)SrbExData)->CdbLength;
                    }

                    if (((PSRBEX_DATA_SCSI_CDB32)SrbExData)->CdbLength > 0) {
                        Cdb = (PCDB)((PSRBEX_DATA_SCSI_CDB32)SrbExData)->Cdb;
                    }

                    if (ScsiStatus) {
                        *ScsiStatus =
                            ((PSRBEX_DATA_SCSI_CDB32)SrbExData)->ScsiStatus;
                    }

                    if (SenseInfoBuffer) {
                        *SenseInfoBuffer =
                            ((PSRBEX_DATA_SCSI_CDB32)SrbExData)->SenseInfoBuffer;
                    }

                    if (SenseInfoBufferLength) {
                        *SenseInfoBufferLength =
                            ((PSRBEX_DATA_SCSI_CDB32)SrbExData)->SenseInfoBufferLength;
                    }

                }
                else {
                    // Catch invalid offset
                    SRBHELPER_ASSERT(FALSE);
                }
                break;

            case SrbExDataTypeScsiCdbVar:
                if (SrbEx->SrbExDataOffset[i] + sizeof(SRBEX_DATA_SCSI_CDB_VAR) <= SrbEx->SrbLength) {
                    FoundEntry = TRUE;
                    if (CdbLength32) {
                        *CdbLength32 = ((PSRBEX_DATA_SCSI_CDB_VAR)SrbExData)->CdbLength;
                    }

                    if (((PSRBEX_DATA_SCSI_CDB_VAR)SrbExData)->CdbLength > 0) {
                        Cdb = (PCDB)((PSRBEX_DATA_SCSI_CDB_VAR)SrbExData)->Cdb;
                    }

                    if (ScsiStatus) {
                        *ScsiStatus =
                            ((PSRBEX_DATA_SCSI_CDB_VAR)SrbExData)->ScsiStatus;
                    }

                    if (SenseInfoBuffer) {
                        *SenseInfoBuffer =
                            ((PSRBEX_DATA_SCSI_CDB_VAR)SrbExData)->SenseInfoBuffer;
                    }

                    if (SenseInfoBufferLength) {
                        *SenseInfoBufferLength =
                            ((PSRBEX_DATA_SCSI_CDB_VAR)SrbExData)->SenseInfoBufferLength;
                    }

                }
                else {
                    // Catch invalid offset
                    SRBHELPER_ASSERT(FALSE);
                }
                break;
            }

            if (FoundEntry) {
                break;
            }
        }

    }
    else {

        if (CdbLength8) {
            *CdbLength8 = 0;
        }

        if (CdbLength32) {
            *CdbLength32 = 0;
        }

        if (ScsiStatus) {
            *ScsiStatus = SCSISTAT_GOOD;
        }

        if (SenseInfoBuffer) {
            *SenseInfoBuffer = NULL;
        }

        if (SenseInfoBufferLength) {
            *SenseInfoBufferLength = 0;
        }
    }

    return Cdb;
}

FORCEINLINE VOID
SrbSetScsiData(
    _In_ PSTORAGE_REQUEST_BLOCK SrbEx,
    _In_opt_ PUCHAR CdbLength8,
    _In_opt_ PULONG CdbLength32,
    _In_opt_ PUCHAR ScsiStatus,
    _In_opt_ PVOID* SenseInfoBuffer,
    _In_opt_ PUCHAR SenseInfoBufferLength
)
/*++

Routine Description:

    Helper function to set SCSI related fields in an extended SRB. If SRB is
    not a SRB_FUNCTION_EXECUTE_SCSI or not an extended SRB, nothing will happen.

    Only the arguments specified will be set in the extended SRB.

Arguments:

    SrbEx - Pointer to extended SRB.

    CdbLength8 - Pointer to buffer that holds the CdbLength field value for
                 SRBEX_DATA_SCSI_CDB16 or SRBEX_DATA_SCSI_CDB32

    CdbLength32 - Pointer to buffer that holds the CdbLength field value for
                  SRBEX_DATA_SCSI_CDB_VAR

    ScsiStatus - Pointer to buffer that holds the ScsiStatus field value.

    SenseInfoBuffer - Pointer to a SenseInfoBuffer pointer.

    SenseInfoBufferLength - Pointer to buffer that holds the SenseInfoBufferLength value.

--*/
{
    ULONG i;
    PSRBEX_DATA SrbExData = NULL;
    BOOLEAN FoundEntry = FALSE;

    if ((SrbEx->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK) &&
        (SrbEx->SrbFunction == SRB_FUNCTION_EXECUTE_SCSI)) {
        SRBHELPER_ASSERT(SrbEx->NumSrbExData > 0);

        for (i = 0; i < SrbEx->NumSrbExData; i++) {

            // Skip any invalid offsets
            if ((SrbEx->SrbExDataOffset[i] < sizeof(STORAGE_REQUEST_BLOCK)) ||
                (SrbEx->SrbExDataOffset[i] >= SrbEx->SrbLength)) {
                // Catch any invalid offsets
                SRBHELPER_ASSERT(FALSE);
                continue;
            }

            SrbExData = (PSRBEX_DATA)((PUCHAR)SrbEx + SrbEx->SrbExDataOffset[i]);

            switch (SrbExData->Type) {

            case SrbExDataTypeScsiCdb16:
                if (SrbEx->SrbExDataOffset[i] + sizeof(SRBEX_DATA_SCSI_CDB16) <= SrbEx->SrbLength) {
                    FoundEntry = TRUE;
                    if (CdbLength8) {
                        ((PSRBEX_DATA_SCSI_CDB16)SrbExData)->CdbLength = *CdbLength8;
                    }

                    if (ScsiStatus) {
                        ((PSRBEX_DATA_SCSI_CDB16)SrbExData)->ScsiStatus = *ScsiStatus;
                    }

                    if (SenseInfoBuffer) {
                        ((PSRBEX_DATA_SCSI_CDB16)SrbExData)->SenseInfoBuffer = *SenseInfoBuffer;
                    }

                    if (SenseInfoBufferLength) {
                        ((PSRBEX_DATA_SCSI_CDB16)SrbExData)->SenseInfoBufferLength = *SenseInfoBufferLength;
                    }

                }
                else {
                    // Catch invalid offset
                    SRBHELPER_ASSERT(FALSE);
                }
                break;

            case SrbExDataTypeScsiCdb32:
                if (SrbEx->SrbExDataOffset[i] + sizeof(SRBEX_DATA_SCSI_CDB32) <= SrbEx->SrbLength) {
                    FoundEntry = TRUE;
                    if (CdbLength8) {
                        ((PSRBEX_DATA_SCSI_CDB32)SrbExData)->CdbLength = *CdbLength8;
                    }

                    if (ScsiStatus) {
                        ((PSRBEX_DATA_SCSI_CDB32)SrbExData)->ScsiStatus = *ScsiStatus;
                    }

                    if (SenseInfoBuffer) {
                        ((PSRBEX_DATA_SCSI_CDB32)SrbExData)->SenseInfoBuffer = *SenseInfoBuffer;
                    }

                    if (SenseInfoBufferLength) {
                        ((PSRBEX_DATA_SCSI_CDB32)SrbExData)->SenseInfoBufferLength = *SenseInfoBufferLength;
                    }

                }
                else {
                    // Catch invalid offset
                    SRBHELPER_ASSERT(FALSE);
                }
                break;

            case SrbExDataTypeScsiCdbVar:
                if (SrbEx->SrbExDataOffset[i] + sizeof(SRBEX_DATA_SCSI_CDB_VAR) <= SrbEx->SrbLength) {
                    FoundEntry = TRUE;
                    if (CdbLength32) {
                        ((PSRBEX_DATA_SCSI_CDB_VAR)SrbExData)->CdbLength = *CdbLength32;
                    }

                    if (ScsiStatus) {
                        ((PSRBEX_DATA_SCSI_CDB_VAR)SrbExData)->ScsiStatus = *ScsiStatus;
                    }

                    if (SenseInfoBuffer) {
                        ((PSRBEX_DATA_SCSI_CDB_VAR)SrbExData)->SenseInfoBuffer = *SenseInfoBuffer;
                    }

                    if (SenseInfoBufferLength) {
                        ((PSRBEX_DATA_SCSI_CDB_VAR)SrbExData)->SenseInfoBufferLength = *SenseInfoBufferLength;
                    }

                }
                else {
                    // Catch invalid offset
                    SRBHELPER_ASSERT(FALSE);
                }
                break;
            }

            if (FoundEntry) {
                break;
            }
        }
    }
}

FORCEINLINE PCDB
SrbGetCdb(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    PCDB pCdb = NULL;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        return SrbGetScsiData(srb,
            NULL,    // CdbLength8
            NULL,    // CdbLength32
            NULL,    // ScsiStatus
            NULL,    // SenseInfoBuffer
            NULL);   // SenseInfoBufferLength
    }
    else
    {
        pCdb = (PCDB)((PSCSI_REQUEST_BLOCK)srb)->Cdb;
    }
    return pCdb;
}

FORCEINLINE ULONG
SrbGetSrbFunction(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        return srb->SrbFunction;
    }
    else
    {
        return (ULONG)((PSCSI_REQUEST_BLOCK)srb)->Function;
    }
}

FORCEINLINE PVOID
SrbGetSenseInfoBuffer(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    PVOID pSenseInfoBuffer = NULL;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        SrbGetScsiData(srb,
            NULL,    // CdbLength8
            NULL,    // CdbLength32
            NULL,    // ScsiStatus
            &pSenseInfoBuffer,   // SenseInfoBuffer
            NULL);   // SenseInfoBufferLength
    }
    else
    {
        pSenseInfoBuffer = ((PSCSI_REQUEST_BLOCK)srb)->SenseInfoBuffer;
    }
    return pSenseInfoBuffer;
}

FORCEINLINE UCHAR
SrbGetSenseInfoBufferLength(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    UCHAR SenseInfoBufferLength = 0;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        SrbGetScsiData(srb,
            NULL,    // CdbLength8
            NULL,    // CdbLength32
            NULL,    // ScsiStatus
            NULL,    // SenseInfoBuffer
            &SenseInfoBufferLength); // SenseInfoBufferLength
    }
    else
    {
        SenseInfoBufferLength = ((PSCSI_REQUEST_BLOCK)srb)->SenseInfoBufferLength;
    }
    return SenseInfoBufferLength;
}

FORCEINLINE VOID
SrbSetSenseInfoBuffer(
    _In_ PVOID Srb,
    _In_opt_ PVOID SenseInfoBuffer
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        SrbSetScsiData(srb,
            NULL,    // CdbLength8
            NULL,    // CdbLength32
            NULL,    // ScsiStatus
            &SenseInfoBuffer, // SenseInfoBuffer
            NULL);   // SenseInfoBufferLength
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->SenseInfoBuffer = SenseInfoBuffer;
    }
}

FORCEINLINE VOID
SrbSetSenseInfoBufferLength(
    _In_ PVOID Srb,
    _In_ UCHAR SenseInfoBufferLength
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        SrbSetScsiData(srb,
            NULL,    // CdbLength8
            NULL,    // CdbLength32
            NULL,    // ScsiStatus
            NULL,    // SenseInfoBuffer
            &SenseInfoBufferLength); // SenseInfoBufferLength
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->SenseInfoBufferLength = SenseInfoBufferLength;
    }
}

FORCEINLINE PVOID
SrbGetOriginalRequest(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        return srb->OriginalRequest;
    }
    else
    {
        return ((PSCSI_REQUEST_BLOCK)srb)->OriginalRequest;
    }
}

FORCEINLINE VOID
SrbSetOriginalRequest(
    _In_ PVOID Srb,
    _In_opt_ PVOID OriginalRequest
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->OriginalRequest = OriginalRequest;
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->OriginalRequest = OriginalRequest;
    }
}

FORCEINLINE PVOID
SrbGetDataBuffer(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    PVOID DataBuffer;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        DataBuffer = srb->DataBuffer;
    }
    else
    {
        DataBuffer = ((PSCSI_REQUEST_BLOCK)srb)->DataBuffer;
    }
    return DataBuffer;
}

FORCEINLINE VOID
SrbSetDataBuffer(
    _In_ PVOID Srb,
    _In_opt_ __drv_aliasesMem PVOID DataBuffer
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->DataBuffer = DataBuffer;
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->DataBuffer = DataBuffer;
    }
}

FORCEINLINE ULONG
SrbGetDataTransferLength(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    ULONG DataTransferLength;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        DataTransferLength = srb->DataTransferLength;
    }
    else
    {
        DataTransferLength = ((PSCSI_REQUEST_BLOCK)srb)->DataTransferLength;
    }
    return DataTransferLength;

}

FORCEINLINE VOID
SrbSetDataTransferLength(
    _In_ PVOID Srb,
    _In_ ULONG DataTransferLength
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->DataTransferLength = DataTransferLength;
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->DataTransferLength = DataTransferLength;
    }
}

FORCEINLINE ULONG
SrbGetTimeOutValue(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    ULONG timeOutValue;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        timeOutValue = srb->TimeOutValue;
    }
    else
    {
        timeOutValue = ((PSCSI_REQUEST_BLOCK)srb)->TimeOutValue;
    }
    return timeOutValue;
}

FORCEINLINE VOID
SrbSetTimeOutValue(
    _In_ PVOID Srb,
    _In_ ULONG TimeOutValue
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->TimeOutValue = TimeOutValue;
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->TimeOutValue = TimeOutValue;
    }
}

FORCEINLINE VOID
SrbSetQueueSortKey(
    _In_ PVOID Srb,
    _In_ ULONG QueueSortKey
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        // Deprecated
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->QueueSortKey = QueueSortKey;
    }
}

FORCEINLINE VOID
SrbSetQueueTag(
    _In_ PVOID Srb,
    _In_ ULONG QueueTag
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->RequestTag = QueueTag;
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->QueueTag = (UCHAR)QueueTag;
    }
}

#define SrbSetRequestTag SrbSetQueueTag

FORCEINLINE ULONG
SrbGetQueueTag(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        return srb->RequestTag;
    }
    else
    {
        return ((PSCSI_REQUEST_BLOCK)srb)->QueueTag;
    }
}

#define SrbGetRequestTag SrbGetQueueTag

FORCEINLINE PVOID
SrbGetNextSrb(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        return (PVOID)srb->NextSrb;
    }
    else
    {
        return (PVOID)((PSCSI_REQUEST_BLOCK)srb)->NextSrb;
    }
}

FORCEINLINE VOID
SrbSetNextSrb(
    _In_ PVOID Srb,
    _In_opt_ PVOID NextSrb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->NextSrb = (PSTORAGE_REQUEST_BLOCK)NextSrb;
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->NextSrb = (PSCSI_REQUEST_BLOCK)NextSrb;
    }
}

FORCEINLINE ULONG
SrbGetSrbFlags(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    ULONG srbFlags;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srbFlags = srb->SrbFlags;
    }
    else
    {
        srbFlags = ((PSCSI_REQUEST_BLOCK)srb)->SrbFlags;
    }
    return srbFlags;
}

FORCEINLINE VOID
SrbAssignSrbFlags(
    _In_ PVOID Srb,
    _In_ ULONG Flags
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->SrbFlags = Flags;
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->SrbFlags = Flags;
    }
}

FORCEINLINE VOID
SrbSetSrbFlags(
    _In_ PVOID Srb,
    _In_ ULONG Flags
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->SrbFlags |= Flags;
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->SrbFlags |= Flags;
    }
}

FORCEINLINE VOID
SrbClearSrbFlags(
    _In_ PVOID Srb,
    _In_ ULONG Flags
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->SrbFlags &= ~Flags;
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->SrbFlags &= ~Flags;
    }
}

FORCEINLINE ULONG
SrbGetSystemStatus(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    ULONG systemStatus;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        systemStatus = srb->SystemStatus;
    }
    else
    {
        systemStatus = ((PSCSI_REQUEST_BLOCK)srb)->InternalStatus;
    }
    return systemStatus;

}

FORCEINLINE VOID
SrbSetSystemStatus(
    _In_ PVOID Srb,
    _In_ ULONG Status
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->SystemStatus = Status;
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->InternalStatus = Status;
    }
}

FORCEINLINE UCHAR
SrbGetScsiStatus(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    UCHAR scsiStatus = 0;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        SrbGetScsiData(srb,
            NULL,    // CdbLength8
            NULL,    // CdbLength32
            &scsiStatus,    // ScsiStatus
            NULL,    // SenseInfoBuffer
            NULL);   // SenseInfoBufferLength
    }
    else
    {
        scsiStatus = ((PSCSI_REQUEST_BLOCK)srb)->ScsiStatus;
    }
    return scsiStatus;
}

FORCEINLINE VOID
SrbSetScsiStatus(
    _In_ PVOID Srb,
    _In_ UCHAR ScsiStatus
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        SrbSetScsiData(srb,
            NULL,    // CdbLength8
            NULL,    // CdbLength32
            &ScsiStatus,    // ScsiStatus
            NULL,    // SenseInfoBuffer
            NULL);   // SenseInfoBufferLength
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->ScsiStatus = ScsiStatus;
    }
}

FORCEINLINE UCHAR
SrbGetCdbLength(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    UCHAR CdbLength = 0;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        SrbGetScsiData(srb,
            &CdbLength,    // CdbLength8
            NULL,    // CdbLength32
            NULL,    // ScsiStatus
            NULL,    // SenseInfoBuffer
            NULL);   // SenseInfoBufferLength
    }
    else
    {
        CdbLength = ((PSCSI_REQUEST_BLOCK)srb)->CdbLength;
    }
    return CdbLength;
}

FORCEINLINE VOID
SrbSetCdbLength(
    _In_ PVOID Srb,
    _In_ UCHAR CdbLength
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        SrbSetScsiData(srb,
            &CdbLength,    // CdbLength8
            NULL,    // CdbLength32
            NULL,    // ScsiStatus
            NULL,    // SenseInfoBuffer
            NULL);   // SenseInfoBufferLength
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->CdbLength = CdbLength;
    }
}

FORCEINLINE ULONG
SrbGetRequestAttribute(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    ULONG RequestAttribute;
    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        RequestAttribute = srb->RequestAttribute;
    }
    else
    {
        RequestAttribute = ((PSCSI_REQUEST_BLOCK)srb)->QueueAction;
    }
    return RequestAttribute;
}

#define SrbGetQueueAction SrbGetRequestAttribute

FORCEINLINE VOID
SrbSetRequestAttribute(
    _In_ PVOID Srb,
    _In_ UCHAR RequestAttribute
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->RequestAttribute = RequestAttribute;
    }
    else
    {
        ((PSCSI_REQUEST_BLOCK)srb)->QueueAction = RequestAttribute;
    }
}

#define SrbSetQueueAction SrbSetRequestAttribute

FORCEINLINE UCHAR
SrbGetPathId(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    UCHAR PathId = 0;
    PSTOR_ADDRESS storAddr = NULL;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        storAddr = (PSTOR_ADDRESS)SrbGetAddress(srb);
        if (storAddr)
        {
            switch (storAddr->Type)
            {
            case STOR_ADDRESS_TYPE_BTL8:
                PathId = ((PSTOR_ADDR_BTL8)storAddr)->Path;
                break;

            default:
                SRBHELPER_ASSERT(FALSE);
                break;
            }
        }
    }
    else
    {
        PathId = ((PSCSI_REQUEST_BLOCK)srb)->PathId;
    }
    return PathId;

}

FORCEINLINE UCHAR
SrbGetTargetId(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    UCHAR TargetId = 0;
    PSTOR_ADDRESS storAddr = NULL;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        storAddr = (PSTOR_ADDRESS)SrbGetAddress(srb);
        if (storAddr)
        {
            switch (storAddr->Type)
            {
            case STOR_ADDRESS_TYPE_BTL8:
                TargetId = ((PSTOR_ADDR_BTL8)storAddr)->Target;
                break;

            default:
                SRBHELPER_ASSERT(FALSE);
                break;
            }
        }
    }
    else
    {
        TargetId = ((PSCSI_REQUEST_BLOCK)srb)->TargetId;
    }
    return TargetId;

}

FORCEINLINE UCHAR
SrbGetLun(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    UCHAR Lun = 0;
    PSTOR_ADDRESS storAddr = NULL;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        storAddr = (PSTOR_ADDRESS)SrbGetAddress(srb);
        if (storAddr)
        {
            switch (storAddr->Type)
            {
            case STOR_ADDRESS_TYPE_BTL8:
                Lun = ((PSTOR_ADDR_BTL8)storAddr)->Lun;
                break;

            default:
                SRBHELPER_ASSERT(FALSE);
                break;
            }
        }
    }
    else
    {
        Lun = ((PSCSI_REQUEST_BLOCK)srb)->Lun;
    }
    return Lun;
}

FORCEINLINE VOID
SrbGetPathTargetLun(
    _In_ PVOID Srb,
    _In_opt_ PUCHAR PathId,
    _In_opt_ PUCHAR TargetId,
    _In_opt_ PUCHAR Lun
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;
    PSTOR_ADDRESS storAddr = NULL;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        storAddr = (PSTOR_ADDRESS)SrbGetAddress(srb);
        if (storAddr)
        {
            switch (storAddr->Type)
            {
            case STOR_ADDRESS_TYPE_BTL8:
                if (PathId != NULL) {
                    *PathId = ((PSTOR_ADDR_BTL8)storAddr)->Path;
                }

                if (TargetId != NULL) {
                    *TargetId = ((PSTOR_ADDR_BTL8)storAddr)->Target;
                }

                if (Lun != NULL) {
                    *Lun = ((PSTOR_ADDR_BTL8)storAddr)->Lun;
                }

                break;

            default:
                SRBHELPER_ASSERT(FALSE);
                break;
            }
        }
    }
    else
    {
        if (PathId != NULL) {
            *PathId = ((PSCSI_REQUEST_BLOCK)srb)->PathId;
        }

        if (TargetId != NULL) {
            *TargetId = ((PSCSI_REQUEST_BLOCK)srb)->TargetId;
        }

        if (Lun != NULL) {
            *Lun = ((PSCSI_REQUEST_BLOCK)srb)->Lun;
        }
    }

    return;
}

FORCEINLINE PVOID
SrbGetMiniportContext(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        return srb->MiniportContext;
    }
    else
    {
        return ((PSCSI_REQUEST_BLOCK)srb)->SrbExtension;
    }
}

FORCEINLINE UCHAR
SrbGetSrbStatus(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        return srb->SrbStatus;
    }
    else
    {
        return ((PSCSI_REQUEST_BLOCK)srb)->SrbStatus;
    }

}

FORCEINLINE VOID
SrbSetSrbStatus(
    _In_ PVOID Srb,
    _In_ UCHAR status
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        if (srb->SrbStatus & SRB_STATUS_AUTOSENSE_VALID)
        {
            srb->SrbStatus = status | SRB_STATUS_AUTOSENSE_VALID;
        }
        else
        {
            srb->SrbStatus = status;
        }
    }
    else
    {
        if (((PSCSI_REQUEST_BLOCK)srb)->SrbStatus & SRB_STATUS_AUTOSENSE_VALID)
        {
            ((PSCSI_REQUEST_BLOCK)srb)->SrbStatus = status | SRB_STATUS_AUTOSENSE_VALID;
        }
        else
        {
            ((PSCSI_REQUEST_BLOCK)srb)->SrbStatus = status;
        }
    }
}

FORCEINLINE PVOID
SrbGetPortContext(
    _In_ PVOID Srb
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        return srb->PortContext;
    }
    else
    {
        SRBHELPER_ASSERT(FALSE);
        return NULL;
    }
}

FORCEINLINE VOID
SrbSetPortContext(
    _In_ PVOID Srb,
    _In_ PVOID PortContext
)
{
    PSTORAGE_REQUEST_BLOCK srb = (PSTORAGE_REQUEST_BLOCK)Srb;

    if (srb->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    {
        srb->PortContext = PortContext;
    }
    else
    {
        SRBHELPER_ASSERT(FALSE);
    }
}


FORCEINLINE VOID
SrbCdbGetRange(
    _In_ PCDB Cdb,
    _In_ PUINT64 POffset,
    _In_ PUINT32 PLength,
    _In_ PUINT32 PForceUnitAccess)
{
    ASSERT(
        SCSIOP_READ6 == Cdb->AsByte[0] ||
        SCSIOP_READ == Cdb->AsByte[0] ||
        SCSIOP_READ12 == Cdb->AsByte[0] ||
        SCSIOP_READ16 == Cdb->AsByte[0] ||
        SCSIOP_WRITE6 == Cdb->AsByte[0] ||
        SCSIOP_WRITE == Cdb->AsByte[0] ||
        SCSIOP_WRITE12 == Cdb->AsByte[0] ||
        SCSIOP_WRITE16 == Cdb->AsByte[0] ||
        SCSIOP_SYNCHRONIZE_CACHE == Cdb->AsByte[0] ||
        SCSIOP_SYNCHRONIZE_CACHE16 == Cdb->AsByte[0]);

    switch (Cdb->AsByte[0] & 0xE0)
    {
    case 0 << 5:
        /* CDB6 */
        if (0 != POffset)
            *POffset =
            ((UINT64)Cdb->CDB6READWRITE.LogicalBlockMsb1 << 16) |
            ((UINT64)Cdb->CDB6READWRITE.LogicalBlockMsb0 << 8) |
            ((UINT64)Cdb->CDB6READWRITE.LogicalBlockLsb);
        if (0 != PLength)
            *PLength = 0 != Cdb->CDB6READWRITE.TransferBlocks ?
            ((UINT32)Cdb->CDB6READWRITE.TransferBlocks) :
            256;
        if (0 != PForceUnitAccess)
            *PForceUnitAccess = 0;
        break;

    case 1 << 5:
    case 2 << 5:
        /* CDB10 */
        if (0 != POffset)
            *POffset =
                ((UINT64)Cdb->CDB10.LogicalBlockByte0 << 24) |
                ((UINT64)Cdb->CDB10.LogicalBlockByte1 << 16) |
                ((UINT64)Cdb->CDB10.LogicalBlockByte2 << 8) |
                ((UINT64)Cdb->CDB10.LogicalBlockByte3);
        if (0 != PLength)
            *PLength =
                ((UINT32)Cdb->CDB10.TransferBlocksMsb << 8) |
                ((UINT32)Cdb->CDB10.TransferBlocksLsb);
        if (0 != PForceUnitAccess)
            *PForceUnitAccess = Cdb->CDB10.ForceUnitAccess;
        break;

    case 4 << 5:
        /* CDB16 */
        if (0 != POffset)
            *POffset =
                ((UINT64)Cdb->CDB16.LogicalBlock[0] << 56) |
                ((UINT64)Cdb->CDB16.LogicalBlock[1] << 48) |
                ((UINT64)Cdb->CDB16.LogicalBlock[2] << 40) |
                ((UINT64)Cdb->CDB16.LogicalBlock[3] << 32) |
                ((UINT64)Cdb->CDB16.LogicalBlock[4] << 24) |
                ((UINT64)Cdb->CDB16.LogicalBlock[5] << 16) |
                ((UINT64)Cdb->CDB16.LogicalBlock[6] << 8) |
                ((UINT64)Cdb->CDB16.LogicalBlock[7]);
        if (0 != PLength)
            *PLength =
                ((UINT32)Cdb->CDB16.TransferLength[0] << 24) |
                ((UINT32)Cdb->CDB16.TransferLength[1] << 16) |
                ((UINT32)Cdb->CDB16.TransferLength[2] << 8) |
                ((UINT32)Cdb->CDB16.TransferLength[3]);
        if (0 != PForceUnitAccess)
            *PForceUnitAccess = Cdb->CDB16.ForceUnitAccess;
        break;

    case 5 << 5:
        /* CDB12 */
        if (0 != POffset)
            *POffset =
                ((UINT64)Cdb->CDB12.LogicalBlock[0] << 24) |
                ((UINT64)Cdb->CDB12.LogicalBlock[1] << 16) |
                ((UINT64)Cdb->CDB12.LogicalBlock[2] << 8) |
                ((UINT64)Cdb->CDB12.LogicalBlock[3]);
        if (0 != PLength)
            *PLength =
                ((UINT32)Cdb->CDB12.TransferLength[0] << 24) |
                ((UINT32)Cdb->CDB12.TransferLength[1] << 16) |
                ((UINT32)Cdb->CDB12.TransferLength[2] << 8) |
                ((UINT32)Cdb->CDB12.TransferLength[3]);
        if (0 != PForceUnitAccess)
            *PForceUnitAccess = Cdb->CDB12.ForceUnitAccess;
        break;
    }
}

#define CHECK_MODE_SENSE(Cdb, Page) \
    (MODE_SENSE_CHANGEABLE_VALUES == (Cdb)->MODE_SENSE.Pc || \
     (Page != (Cdb)->MODE_SENSE.PageCode && \
      MODE_SENSE_RETURN_ALL != (Cdb)->MODE_SENSE.PageCode))

#define CHECK_MODE_SENSE10(Cdb, Page) \
    (MODE_SENSE_CHANGEABLE_VALUES == (Cdb)->MODE_SENSE10.Pc || \
     (Page != (Cdb)->MODE_SENSE10.PageCode && \
      MODE_SENSE_RETURN_ALL != (Cdb)->MODE_SENSE10.PageCode))

#endif // (NTDDI_VERSION >= NTDDI_WIN8)

#endif // _NTSRBHELPER_
