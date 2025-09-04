#pragma once
#include "plotting/Compression.h"
#include <string>

struct PuzzleHash;
namespace bls
{
    class G1Element;
}

struct GlobalPlotConfig
{
    static constexpr uint32 MAX_OUTPUT_FOLDERS = 32;

    uint32 threadCount   = 0;
    uint32 plotCount     = 1;

    const char* plotIdStr    = nullptr;
    const char* plotMemoStr  = nullptr;

    const char* outputFolder = nullptr;

    uint32       outputFolderCount         = 0;
    std::string  outputFolders[MAX_OUTPUT_FOLDERS];

    bool            benchmarkMode          = false;
    bool            showMemo               = false;
    bool            warmStart              = false;
    bool            disableNuma            = false;
    bool            disableCpuAffinity     = false;
    bool            disableOutputDirectIO  = false;
    bool            verbose                = false;
    uint32          compressionLevel       = 0;
    uint32          compressedEntryBits    = 32;
    FSE_CTable*     ctable                 = nullptr;
    size_t          cTableSize             = 0;
    uint32          numDroppedTables       = 0;
    CompressionInfo compressionInfo        = {};

    bls::G1Element* farmerPublicKey        = nullptr;
    bls::G1Element* poolPublicKey          = nullptr;   // Either poolPublicKey or poolContractPuzzleHash must be set.
    PuzzleHash*     poolContractPuzzleHash = nullptr;   // If both are set, poolContractPuzzleHash will be used over
                                                        // the poolPublicKey.

};

