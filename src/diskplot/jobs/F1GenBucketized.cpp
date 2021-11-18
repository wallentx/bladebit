#include "F1GenBucketized.h"
#include "diskplot/DiskBufferQueue.h"
#include "threading/ThreadPool.h"
#include "pos/chacha8.h"
#include "plotshared/DoubleBuffer.h"

template<typename TJob>
struct F1BucketJob : public MTJob<TJob>
{
    const byte* key;                // Chacha key
    uint32      x;                  // Starting x value for this thread
    uint32      blockCount;         // Total number of blocks for each thread. Or blocks per thread per chunk in the disk version.
    uint32*     counts;             // Each thread's entry count per bucket. Shared with other threads to calculate prefix sum.
    uint32*     totalBucketCounts;  // Total counts per for all buckets. Used by the control thread

    byte*   blocks;                 // chacha blocks buffer
    uint32* yBuckets;               // Bucket buffer for y (on the disk version, these are set by the control thread)
    uint32* xBuckets;               // Bucket buffer for x

    void CalculateMultiThreadedPrefixSum( 
        uint32 counts[BB_DP_BUCKET_COUNT],
        uint32 pfxSum[BB_DP_BUCKET_COUNT],
        const size_t fileBlockSize
    );
};

// Job to perform bucketized f1 generation completely in memory
struct F1MemBucketJob : public F1BucketJob<F1MemBucketJob>
{
    void Run() override;
};

// Job to perform bucketized f1 generation and writing it
// to disk in intervals known as "chunks".
struct F1DiskBucketJob : public F1BucketJob<F1DiskBucketJob>
{
    DiskBufferQueue* diskQueue;

    uint32  chunkCount;
    uint32  chunkBufferSize;    // Allocation size for the chunk buffer.
                                // It should be guaranteed to hold enough values
                                // needed for all buckets, including file block size 
                                // alignment space.

    uint32  trailingBlocks;     // Total amount of trailing blocks (not per-thread),
                                // These fit inside a chunk, but may be less than
                                // can be handled per-thread so we may need to run
                                // less threads for them.

    byte* remaindersBuffer;     // Buffer used to hold the remainder entries that 
                                // don't align to file block size.
                                // Thse are submitted in their own commands when
                                // we have enough entries to align to file block size.

    void Run() override;

    void SaveBlockRemainders( uint32* yBuckets, uint32* xBuckets, const uint32* bucketCounts, 
                              DoubleBuffer* remainderBuffers, uint32* remainderSizes );

    void WriteFinalBlockRemainders( DoubleBuffer* remainderBuffers, uint32* remainderSizes );
};

template<bool WriteToDisk, bool SingleThreaded, typename TJob>
void GenerateF1(
    chacha8_ctx& chacha, 
    uint32       x,
    byte*        blocks,
    uint32       blockCount,
    uint32       entryCount,
    uint32*      buckets,
    uint32*      xBuffer,

    // For MT
    MTJobSyncT<TJob>* job,

    // For writing to disk variant
    size_t  fileBlockSize,
    uint32* sizes
);

//-----------------------------------------------------------
template<typename TJob>
void InitF1BucketJob(
    const byte* key,
    uint        threadCount,
    uint        blocksPerThread,
    uint        trailingBlocks,
    byte*       blocks,
    uint32*     yBuckets,
    uint32*     xBuckets,
    uint32      bucketCounts[BB_DP_BUCKET_COUNT],
    F1BucketJob<TJob>* jobs
)
{
    const uint32 entriesPerBlock = (uint32)( kF1BlockSize / sizeof( uint32 ) );

    uint32 x = 0;

    for( uint i = 0; i < threadCount; i++ )
    {
        auto& job = jobs[i];

        job.key               = key;
        job.blockCount        = blocksPerThread;
        job.x                 = x;
        
        job.blocks            = blocks;
        job.yBuckets          = yBuckets;
        job.xBuckets          = xBuckets;
        job.totalBucketCounts = nullptr;
        job.counts            = nullptr;

        if( trailingBlocks )
        {
            job.blockCount ++;
            trailingBlocks --; 
        }

        x       += job.blockCount * entriesPerBlock;
        blocks  += job.blockCount * kF1BlockSize;
    }

    jobs[0].totalBucketCounts = bucketCounts;
}

///
/// RAM F1
///
//-----------------------------------------------------------
void F1GenBucketized::GenerateF1Mem( 
    const byte* plotId,
    ThreadPool& pool, 
    uint        threadCount,
    byte*       blocks,
    uint32*     yBuckets,
    uint32*     xBuckets,
    uint32      bucketCounts[BB_DP_BUCKET_COUNT]
)
{
    ASSERT( threadCount <= pool.ThreadCount() );
    
    const uint64 entryCount      = 1ull << _K;
    const uint32 entriesPerBlock = (uint32)( kF1BlockSize / sizeof( uint32 ) );
    const uint32 blockCount      = (uint32)( entryCount / entriesPerBlock );
    
    const uint32 blocksPerThread = blockCount / threadCount;

    uint32 trailingBlocks  = blockCount - blocksPerThread * threadCount;
    
    // Prepare ChaCha key
    byte key[32] = { 1 };
    memcpy( key + 1, plotId, 31 );

    memset( bucketCounts, 0, sizeof( uint32 ) * BB_DP_BUCKET_COUNT );

    // uint32 x = 0;
    MTJobRunner<F1MemBucketJob> f1Job( pool );

    InitF1BucketJob<F1MemBucketJob>( key, threadCount, blocksPerThread, trailingBlocks, 
                                     blocks, yBuckets, xBuckets, bucketCounts, f1Job.Jobs() );

    // for( uint i = 0; i < threadCount; i++ )
    // {
    //     F1MemBucketJob& job = f1Job[i];
    //     job.key               = key;
    //     job.blockCount        = blocksPerThread;
    //     job.x                 = x;
        
    //     job.blocks            = blocks;
    //     job.yBuckets          = yBuckets;
    //     job.xBuckets          = xBuckets;
    //     job.totalBucketCounts = nullptr;
    //     job.counts            = nullptr;

    //     if( trailingBlocks )
    //     {
    //         job.blockCount ++;
    //         trailingBlocks --; 
    //     }

    //     x       += job.blockCount * entriesPerBlock;
    //     blocks  += job.blockCount * kF1BlockSize;
    // }

    // f1Job[0].totalBucketCounts = bucketCounts;

    f1Job.Run( threadCount );
}

//-----------------------------------------------------------
void F1MemBucketJob::Run()
{
    chacha8_ctx chacha;
    chacha8_keysetup( &chacha, key, 256, NULL );

    const uint32 entriesPerBlock = kF1BlockSize / sizeof( uint32 );
    const uint32 entryCount      = blockCount * entriesPerBlock;

    // if( this->JobCount() > 1 )
    // MTJobSyncT<F1MemBucketJob>* job = static_cast<MTJobSyncT<F1MemBucketJob>*>( this );
    GenerateF1<false, false>( chacha, x, blocks, 
                              blockCount, entryCount, 
                              yBuckets, xBuckets, this, 0, nullptr );
}



///
/// Disk F1
///
//-----------------------------------------------------------
void F1GenBucketized::GenerateF1Disk(
    const byte*      plotId,
    ThreadPool&      pool, 
    uint             threadCount,
    DiskBufferQueue& diskQueue,
    const size_t     chunkSize,
    uint32           bucketCounts[BB_DP_BUCKET_COUNT]
)
{
    ASSERT( threadCount <= pool.ThreadCount() );
    
    const uint64 entryCount      = 1ull << _K;
    const uint32 entriesPerBlock = (uint32)( kF1BlockSize / sizeof( uint32 ) );
    const uint32 blockCount      = (uint32)( entryCount / entriesPerBlock );

    // We need to adjust the chunk buffer size to leave some space for us to be
    // able to align each bucket start pointer to the block size of the output device
    const size_t fileBlockSize   = diskQueue.BlockSize();
    const size_t usableChunkSize = chunkSize - fileBlockSize * BB_DP_BUCKET_COUNT;

    
    const uint32 blocksPerChunk   = (uint32)( usableChunkSize / ( kF1BlockSize * 2 ) ); // * 2 because we also have space for the x values
    
    uint32 chunkCount     = blockCount / blocksPerChunk;                // How many chunks we need to process 
                                                                        // (needs ot be floored to ensure we fit in the chunk buffer)
    uint32 trailingBlocks = blockCount - blocksPerChunk * chunkCount;

    // Threads operate on a chunk at a time.
    const uint32 blocksPerThread  = blocksPerChunk / threadCount;

    // #TODO: Ensure each thread has at least one block.
    ASSERT( blocksPerThread > 0 );
    
    // Add the blocks that don't into a per-thread boundary as trailing blocks
    trailingBlocks += ( blocksPerChunk * chunkCount ) - ( blocksPerThread * threadCount * chunkCount );

    // If the trailing blocks add up to a new chunk, then add that chunk
    // the rest of the traiiling entries will be run as a special case,
    // potentially with less threads than specified.
    if( trailingBlocks > blocksPerChunk )
    {
        chunkCount ++;
        trailingBlocks -= blocksPerChunk;
    }


    // Allocate our buffers
    byte* blocks = diskQueue.GetBuffer( blocksPerThread * kF1BlockSize * threadCount );

    // Remainders buffers are: 2 fileBlockSized buffers per bucket
    byte* remainders = diskQueue.GetBuffer( diskQueue.BlockSize() * BB_DP_BUCKET_COUNT * 2 );

     // Prepare Jobs
    byte key[32] = { 1 };
    memcpy( key + 1, plotId, 31 );

    memset( bucketCounts, 0, sizeof( uint32 ) * BB_DP_BUCKET_COUNT );

    MTJobRunner<F1DiskBucketJob> f1Job( pool );

    InitF1BucketJob<F1DiskBucketJob>( key, threadCount, blocksPerThread, 0, 
                                      blocks, nullptr, nullptr, bucketCounts, f1Job.Jobs() );

    uint32 x = 0;

    for( uint i = 0; i < threadCount; i++ )
    {
        F1DiskBucketJob& job = f1Job[i];
        
        job.diskQueue      = &diskQueue;
        job.x              = x;
        job.chunkCount     = chunkCount;
        job.trailingBlocks = trailingBlocks;

        x += job.blockCount * entriesPerBlock * chunkCount;
    }

    // Run jobs
    f1Job.Run( threadCount );

    // Release our buffers
    {
        AutoResetSignal fence;

        diskQueue.ReleaseBuffer( blocks     );
        diskQueue.ReleaseBuffer( remainders );
        diskQueue.AddFence( fence );
        diskQueue.CommitCommands();

        fence.Wait();
        diskQueue.CompletePendingReleases();
    }
}

//-----------------------------------------------------------
void F1DiskBucketJob::Run()
{
    DiskBufferQueue& diskQueue = *this->diskQueue;

    const size_t fileBlockSize   = diskQueue.BlockSize();
    const size_t chunkBufferSize = this->chunkBufferSize;
    
    uint32* sizes    = nullptr;      
    uint32* yBuckets = nullptr;
    uint32* xBuckets = nullptr;

    uint32 chunkCount = this->chunkCount;
    uint32 blockCount = this->blockCount;

    const uint32 entriesPerBlock = kF1BlockSize / sizeof( uint32 );
    const uint64 entriesPerChunk = (uint64)blockCount * entriesPerBlock;

    chacha8_ctx chacha;
    chacha8_keysetup( &chacha, key, 256, NULL );

    // These are used only by the control thread
    DoubleBuffer* remainders         = nullptr;
    uint*         remainderSizes     = nullptr;
    uint*         bucketCounts       = nullptr;

    if( this->IsControlThread() )
    {
        remainders         = bbmalloc<DoubleBuffer>( sizeof( DoubleBuffer ) * BB_DP_BUCKET_COUNT );
        remainderSizes     = bbmalloc<uint32>      ( sizeof( uint )         * BB_DP_BUCKET_COUNT );
        bucketCounts       = bbmalloc<uint32>      ( sizeof( uint )         * BB_DP_BUCKET_COUNT );

        memset( remainderSizes, 0, sizeof( uint32 ) * BB_DP_BUCKET_COUNT );

        // Layout for the buffers is:
        //
        // front: [y0][x0][y1][x1]...[y63][x63]
        // back:  [y0][x0][y1][x1]...[y63][x63]
        // 
        // So all 'front' buffers are contiguous for all buckets,
        // then follow all the 'back' buffers for all buckets.
        byte* front = this->remaindersBuffer;
        byte* back  = this->remaindersBuffer + fileBlockSize * BB_DP_BUCKET_COUNT * 2;

        for( uint i = 0; i < BB_DP_BUCKET_COUNT; i++ )
        {
            DoubleBuffer* dbuf = new ( (void*)&remainders[i] ) DoubleBuffer();

            dbuf->front = front;
            dbuf->back  = back;

            front += fileBlockSize * 2;
            back  += fileBlockSize * 2;
        }
    }

    // Add a special-case last chunk if we have trailing blocks 
    // (blocks that don't add-up to completely fill a chunk)
    uint trailingChunk = 0xFFFFFFFF;

    if( this->trailingBlocks )
    {
        trailingChunk = chunkCount;
        chunkCount ++;
    }

    // Start processing chunks
    uint32 x = this->x;

    for( uint chunk = 0; chunk < chunkCount; chunk++ )
    {
        if( chunk == trailingChunk )
        {
            // If this is the trailing chunk, this means it is a special-case
            // chunk which has less blocks than a full chunk. We might need
            // to use less threads, therefore drop the threads that won't participate
            // in the trailing chunk.
            
            uint32 trailingBlocks = this->trailingBlocks;
            uint32 threadCount    = this->JobCount();

            // See how many threads we need for the trailing blocks
            uint32 blocksPerThread = trailingBlocks / threadCount;
        
            while( !blocksPerThread )
            {
                blocksPerThread = trailingBlocks / --threadCount;
            }

            // Change our participating thread counts, and if this thread
            // is no longer participating, exit early
            if( !this->ReduceThreadCount( threadCount ) )
                return;

            // Update the block count
            blockCount = blocksPerThread;

            // OK to process trailing blocks
        }

        // Grab an IO buffer
        if( this->IsControlThread() )
        {
            this->LockThreads();

            sizes = (uint32*)diskQueue.GetBuffer( sizeof( uint32 ) * BB_DP_BUCKET_COUNT );

            byte* bucketBuffer = diskQueue.GetBuffer( chunkBufferSize );
            yBuckets = (uint32*)bucketBuffer;
            xBuckets = (uint32*)( bucketBuffer + blockCount * kF1BlockSize );
            ASSERT( (intptr_t)xBuckets / fileBlockSize * fileBlockSize == (intptr_t)xBuckets );

            this->ReleaseThreads();
        }
        else
        {
            this->WaitForRelease();

            yBuckets = GetJob( 0 ).yBuckets;
            xBuckets = GetJob( 0 ).xBuckets;

            ASSERT( yBuckets );
            ASSERT( xBuckets );
        }

        // Calculate blocks for this chunk
        GenerateF1<true, false>( chacha, x, blocks, blockCount, entriesPerChunk, yBuckets, xBuckets, this, fileBlockSize, sizes );

        // Submit the buckets for this chunk to disk
        // Now this chunk can be submitted to the write queue, and we can continue to the next one.
        // After all the chunks have been written, we can read back from disk to sort each bucket
        // #TODO: Move this to its own func
        
        if( this->LockThreads() )
        {
            // #TODO: Don't do this if not using direct IO?
            // #NOTE: We give it the non-block aligned size, but the Queue will 
            //        only write up to the block aligned size. The rest
            //        we write with the remainder buffers.
            for( uint i = 0; i < BB_DP_BUCKET_COUNT; i++ )
                sizes[i] = (uint32)( bucketCounts[i] * sizeof( uint32 ) );

            diskQueue.WriteBuckets( FileId::Y0, yBuckets, sizes );
            diskQueue.WriteBuckets( FileId::X , xBuckets, sizes );
            diskQueue.CommitCommands();

            // If we're not at our last chunk, we need to shave-off
            // any entries that will not align to the file block size and
            // leave them in our buckets for the next run.
            this->SaveBlockRemainders( yBuckets, xBuckets, bucketCounts, remainders, remainderSizes );

            diskQueue.ReleaseBuffer( sizes    );
            diskQueue.ReleaseBuffer( yBuckets );
            diskQueue.CommitCommands();

            this->ReleaseThreads();
        }
        else
            this->WaitForRelease();
    }
    
    // Write final block remainders & Clean-up
    if( this->IsControlThread() )
    {
        this->WriteFinalBlockRemainders( remainders, remainderSizes );

        // Wait for our commands to finish
        AutoResetSignal fence;
        diskQueue.AddFence( fence );
        diskQueue.CommitCommands();
        fence.Wait();

        // Destruct & free our remainders
        for( uint i = 0; i < BB_DP_BUCKET_COUNT; i++ )
            remainders[i].~DoubleBuffer();

        free( remainders        );
        free( remainderSizes    );
        free( bucketCounts      );
    }
}


//-----------------------------------------------------------
inline void F1DiskBucketJob::SaveBlockRemainders( uint32* yBuckets, uint32* xBuckets, const uint32* bucketCounts, 
                                                  DoubleBuffer* remainderBuffers, uint32* remainderSizes )
{
    DiskBufferQueue& queue               = *this->diskQueue;
    const size_t     fileBlockSize       = queue.BlockSize();
    const size_t     remainderBufferSize = fileBlockSize * BB_DP_BUCKET_COUNT;

    byte* yPtr = (byte*)yBuckets;
    byte* xPtr = (byte*)xBuckets;

    for( uint i = 0; i < BB_DP_BUCKET_COUNT; i++ )
    {
        const size_t bucketSize       = bucketCounts[i] * sizeof( uint32 );
        const size_t blockAlignedSize = bucketSize / fileBlockSize * fileBlockSize;
                    
        size_t remainderSize = bucketSize - blockAlignedSize;
        ASSERT( remainderSize / 4 * 4 == remainderSize );

        if( remainderSize )
        {
            size_t curRemainderSize = remainderSizes[i];
                        
            const size_t copySize = std::min( remainderSize, fileBlockSize - curRemainderSize );

            DoubleBuffer& buf = remainderBuffers[i];

            byte* yRemainder = buf.front;
            byte* xRemainder = yRemainder + fileBlockSize;

            bbmemcpy_t( yRemainder + curRemainderSize, yPtr + blockAlignedSize, copySize );
            bbmemcpy_t( xRemainder + curRemainderSize, xPtr + blockAlignedSize, copySize );

            curRemainderSize += remainderSize;

            if( curRemainderSize >= fileBlockSize )
            {
                // This may block if the last buffer has not yet finished writing to disk
                buf.Flip();

                // Overflow buffer is full, submit it for writing
                queue.WriteFile( FileId::Y0, i, yRemainder, fileBlockSize );
                queue.WriteFile( FileId::X , i, xRemainder, fileBlockSize );
                queue.AddFence( buf.fence );
                queue.CommitCommands();

                // Update new remainder size, if we overflowed our buffer
                // and copy any overflow, if we have some.
                remainderSize = curRemainderSize - fileBlockSize;

                if( remainderSize )
                {
                    yRemainder = buf.front;
                    xRemainder = yRemainder + fileBlockSize;

                    bbmemcpy_t( yRemainder, yPtr + blockAlignedSize + copySize, remainderSize );
                    bbmemcpy_t( xRemainder, xPtr + blockAlignedSize + copySize, remainderSize );
                }

                remainderSizes[i] = 0;
                remainderSize     = remainderSize;
            }

            // Update size
            remainderSizes[i] += (uint)remainderSize;
        }

        // The next bucket buffer starts at the next file block size boundary
        const size_t bucketOffset = RoundUpToNextBoundaryT( bucketSize, fileBlockSize );
        yPtr += bucketOffset;
        xPtr += bucketOffset;
    }
}

//-----------------------------------------------------------
void F1DiskBucketJob::WriteFinalBlockRemainders( DoubleBuffer* remainderBuffers, uint32* remainderSizes )
{
    DiskBufferQueue& queue         = *this->diskQueue;
    const size_t     fileBlockSize = queue.BlockSize();

    for( uint i = 0; i < BB_DP_BUCKET_COUNT; i++ )
    {
        const size_t size = remainderSizes[i];
        ASSERT( size / 4 * 4 == size );
        
        if( size == 0 )
            continue;

        const DoubleBuffer& buf = remainderBuffers[i];
        
        byte* yBuffer = buf.front;
        byte* xBuffer = yBuffer + fileBlockSize;

        queue.WriteFile( FileId::Y0, i, yBuffer, size );
        queue.WriteFile( FileId::X , i, xBuffer, size );
    }
}


///
/// F1 Generation
///
//-----------------------------------------------------------
template<bool WriteToDisk, bool SingleThreaded, typename TJob>
void GenerateF1( 
    chacha8_ctx& chacha, 
    uint32  x,
    byte*   blocks,
    uint32  blockCount,
    uint64  entryCount,
    uint32* yBuckets,
    uint32* xBuckets,
    MTJobSyncT<TJob>* job,
    size_t  fileBlockSize,
    uint32* sizes
    )
{
    const uint64 chachaBlock = ((uint64)x) * _K / kF1BlockSizeBits;

    const uint32 entriesPerBlock   = kF1BlockSize / sizeof( uint32 );
    const uint32 kMinusKExtraBits  = _K - kExtraBits;
    const uint32 bucketShift       = (8u - (uint)kExtraBits);

    const uint32 jobId             = job->JobId();
    const uint32 jobCount          = job->JobCount();

    ASSERT( entryCount <= (uint64)blockCount * entriesPerBlock );
    
    // Generate chacha blocks
    chacha8_get_keystream( &chacha, chachaBlock, blockCount, blocks );

    // Count how many entries we have per bucket
    uint counts[BB_DP_BUCKET_COUNT];
    uint pfxSum[BB_DP_BUCKET_COUNT];

    memset( counts, 0, sizeof( counts ) );

    const uint32* block = (uint32*)blocks;

    // Count entries per bucket. Only calculate the blocks that have full entries
    const uint32 fullBlockCount  = entryCount / entriesPerBlock;
    const uint64 trailingEntries = (uint32)( blockCount * (uint64)entriesPerBlock - entryCount );

    for( uint i = 0; i < fullBlockCount; i++ )
    {
        // Unroll a whole block

        // Determine the bucket id by grabbing the lowest kExtrabits, the highest
        // kExtraBits from the LSB. This is equivalent to the kExtraBits MSbits of the entry
        // once it is endian-swapped later.
        // 0x3F == 6 bits( kExtraBits )
        const uint32 e0  = ( block[0 ] >> bucketShift ) & 0x3F; ASSERT( e0  <= 0b111111u );
        const uint32 e1  = ( block[1 ] >> bucketShift ) & 0x3F; ASSERT( e1  <= 0b111111u );
        const uint32 e2  = ( block[2 ] >> bucketShift ) & 0x3F; ASSERT( e2  <= 0b111111u );
        const uint32 e3  = ( block[3 ] >> bucketShift ) & 0x3F; ASSERT( e3  <= 0b111111u );
        const uint32 e4  = ( block[4 ] >> bucketShift ) & 0x3F; ASSERT( e4  <= 0b111111u );
        const uint32 e5  = ( block[5 ] >> bucketShift ) & 0x3F; ASSERT( e5  <= 0b111111u );
        const uint32 e6  = ( block[6 ] >> bucketShift ) & 0x3F; ASSERT( e6  <= 0b111111u );
        const uint32 e7  = ( block[7 ] >> bucketShift ) & 0x3F; ASSERT( e7  <= 0b111111u );
        const uint32 e8  = ( block[8 ] >> bucketShift ) & 0x3F; ASSERT( e8  <= 0b111111u );
        const uint32 e9  = ( block[9 ] >> bucketShift ) & 0x3F; ASSERT( e9  <= 0b111111u );
        const uint32 e10 = ( block[10] >> bucketShift ) & 0x3F; ASSERT( e10 <= 0b111111u );
        const uint32 e11 = ( block[11] >> bucketShift ) & 0x3F; ASSERT( e11 <= 0b111111u );
        const uint32 e12 = ( block[12] >> bucketShift ) & 0x3F; ASSERT( e12 <= 0b111111u );
        const uint32 e13 = ( block[13] >> bucketShift ) & 0x3F; ASSERT( e13 <= 0b111111u );
        const uint32 e14 = ( block[14] >> bucketShift ) & 0x3F; ASSERT( e14 <= 0b111111u );
        const uint32 e15 = ( block[15] >> bucketShift ) & 0x3F; ASSERT( e15 <= 0b111111u );

        counts[e0 ] ++;
        counts[e1 ] ++;
        counts[e2 ] ++;
        counts[e3 ] ++;
        counts[e4 ] ++;
        counts[e5 ] ++;
        counts[e6 ] ++;
        counts[e7 ] ++;
        counts[e8 ] ++;
        counts[e9 ] ++;
        counts[e10] ++;
        counts[e11] ++;
        counts[e12] ++;
        counts[e13] ++;
        counts[e14] ++;
        counts[e15] ++;

        block += entriesPerBlock;
    }

    // Process trailing entries
    for( uint64 i = 0; i < trailingEntries; i++ )
        counts[( block[i] >> bucketShift ) & 0x3F] ++;

    // Calculate the prefix sum for our buckets
    if constexpr ( SingleThreaded )
    {
        memcpy( pfxSum, counts, sizeof( counts ) );

        if constexpr ( WriteToDisk )
        {
            // We need to align each count to file block size
            // so that each bucket starts aligned 
            // (we won't write submit these extra false entries)
            for( uint i = 0; i < BB_DP_BUCKET_COUNT; i++ )
                pfxSum[i] = RoundUpToNextBoundary( pfxSum[i] * sizeof( uint32 ), (int)fileBlockSize ) / sizeof( uint32 );
        }

        for( uint i = 1; i < BB_DP_BUCKET_COUNT; i++ )
            pfxSum[i] += pfxSum[i-1];
    }
    else
    {
        static_cast<F1BucketJob<TJob>*>( job )->CalculateMultiThreadedPrefixSum( counts, pfxSum, fileBlockSize );
    }
    
    // Now we know the offset where we can start distributing
    // y and x values to their respective buckets.
    block = (uint*)blocks;

    for( uint i = 0; i < fullBlockCount; i++ )
    {
        // chacha output is treated as big endian, therefore swap, as required by chiapos
        const uint32 y0  = Swap32( block[0 ] );
        const uint32 y1  = Swap32( block[1 ] );
        const uint32 y2  = Swap32( block[2 ] );
        const uint32 y3  = Swap32( block[3 ] );
        const uint32 y4  = Swap32( block[4 ] );
        const uint32 y5  = Swap32( block[5 ] );
        const uint32 y6  = Swap32( block[6 ] );
        const uint32 y7  = Swap32( block[7 ] );
        const uint32 y8  = Swap32( block[8 ] );
        const uint32 y9  = Swap32( block[9 ] );
        const uint32 y10 = Swap32( block[10] );
        const uint32 y11 = Swap32( block[11] );
        const uint32 y12 = Swap32( block[12] );
        const uint32 y13 = Swap32( block[13] );
        const uint32 y14 = Swap32( block[14] );
        const uint32 y15 = Swap32( block[15] );

        const uint32 idx0  = --pfxSum[y0  >> kMinusKExtraBits];
        const uint32 idx1  = --pfxSum[y1  >> kMinusKExtraBits];
        const uint32 idx2  = --pfxSum[y2  >> kMinusKExtraBits];
        const uint32 idx3  = --pfxSum[y3  >> kMinusKExtraBits];
        const uint32 idx4  = --pfxSum[y4  >> kMinusKExtraBits];
        const uint32 idx5  = --pfxSum[y5  >> kMinusKExtraBits];
        const uint32 idx6  = --pfxSum[y6  >> kMinusKExtraBits];
        const uint32 idx7  = --pfxSum[y7  >> kMinusKExtraBits];
        const uint32 idx8  = --pfxSum[y8  >> kMinusKExtraBits];
        const uint32 idx9  = --pfxSum[y9  >> kMinusKExtraBits];
        const uint32 idx10 = --pfxSum[y10 >> kMinusKExtraBits];
        const uint32 idx11 = --pfxSum[y11 >> kMinusKExtraBits];
        const uint32 idx12 = --pfxSum[y12 >> kMinusKExtraBits];
        const uint32 idx13 = --pfxSum[y13 >> kMinusKExtraBits];
        const uint32 idx14 = --pfxSum[y14 >> kMinusKExtraBits];
        const uint32 idx15 = --pfxSum[y15 >> kMinusKExtraBits];

        // Add the x as the kExtraBits, and strip away the high kExtraBits,
        // which is now our bucket id, and place each entry into it's respective bucket
        // #NOTE: False sharing can occur here
        yBuckets[idx0 ] = ( y0  << kExtraBits ) | ( ( x + 0  ) >> kMinusKExtraBits );
        yBuckets[idx1 ] = ( y1  << kExtraBits ) | ( ( x + 1  ) >> kMinusKExtraBits );
        yBuckets[idx2 ] = ( y2  << kExtraBits ) | ( ( x + 2  ) >> kMinusKExtraBits );
        yBuckets[idx3 ] = ( y3  << kExtraBits ) | ( ( x + 3  ) >> kMinusKExtraBits );
        yBuckets[idx4 ] = ( y4  << kExtraBits ) | ( ( x + 4  ) >> kMinusKExtraBits );
        yBuckets[idx5 ] = ( y5  << kExtraBits ) | ( ( x + 5  ) >> kMinusKExtraBits );
        yBuckets[idx6 ] = ( y6  << kExtraBits ) | ( ( x + 6  ) >> kMinusKExtraBits );
        yBuckets[idx7 ] = ( y7  << kExtraBits ) | ( ( x + 7  ) >> kMinusKExtraBits );
        yBuckets[idx8 ] = ( y8  << kExtraBits ) | ( ( x + 8  ) >> kMinusKExtraBits );
        yBuckets[idx9 ] = ( y9  << kExtraBits ) | ( ( x + 9  ) >> kMinusKExtraBits );
        yBuckets[idx10] = ( y10 << kExtraBits ) | ( ( x + 10 ) >> kMinusKExtraBits );
        yBuckets[idx11] = ( y11 << kExtraBits ) | ( ( x + 11 ) >> kMinusKExtraBits );
        yBuckets[idx12] = ( y12 << kExtraBits ) | ( ( x + 12 ) >> kMinusKExtraBits );
        yBuckets[idx13] = ( y13 << kExtraBits ) | ( ( x + 13 ) >> kMinusKExtraBits );
        yBuckets[idx14] = ( y14 << kExtraBits ) | ( ( x + 14 ) >> kMinusKExtraBits );
        yBuckets[idx15] = ( y15 << kExtraBits ) | ( ( x + 15 ) >> kMinusKExtraBits );

        // Store the x that generated this y
        xBuckets[idx0 ] = x + 0 ;
        xBuckets[idx1 ] = x + 1 ;
        xBuckets[idx2 ] = x + 2 ;
        xBuckets[idx3 ] = x + 3 ;
        xBuckets[idx4 ] = x + 4 ;
        xBuckets[idx5 ] = x + 5 ;
        xBuckets[idx6 ] = x + 6 ;
        xBuckets[idx7 ] = x + 7 ;
        xBuckets[idx8 ] = x + 8 ;
        xBuckets[idx9 ] = x + 9 ;
        xBuckets[idx10] = x + 10;
        xBuckets[idx11] = x + 11;
        xBuckets[idx12] = x + 12;
        xBuckets[idx13] = x + 13;
        xBuckets[idx14] = x + 14;
        xBuckets[idx15] = x + 15;

        // const uint32 refY = 27327;
        // if( buckets[idx0 ] == refY ) BBDebugBreak();
        // if( buckets[idx1 ] == refY ) BBDebugBreak();
        // if( buckets[idx2 ] == refY ) BBDebugBreak();
        // if( buckets[idx3 ] == refY ) BBDebugBreak();
        // if( buckets[idx4 ] == refY ) BBDebugBreak();
        // if( buckets[idx5 ] == refY ) BBDebugBreak();
        // if( buckets[idx6 ] == refY ) BBDebugBreak();
        // if( buckets[idx7 ] == refY ) BBDebugBreak();
        // if( buckets[idx8 ] == refY ) BBDebugBreak();
        // if( buckets[idx9 ] == refY ) BBDebugBreak();
        // if( buckets[idx10] == refY ) BBDebugBreak();
        // if( buckets[idx11] == refY ) BBDebugBreak();
        // if( buckets[idx12] == refY ) BBDebugBreak();
        // if( buckets[idx13] == refY ) BBDebugBreak();
        // if( buckets[idx14] == refY ) BBDebugBreak();
        // if( buckets[idx15] == refY ) BBDebugBreak();

        // if( x + 0  == 2853878795 ) BBDebugBreak();
        // if( x + 1  == 2853878795 ) BBDebugBreak();
        // if( x + 2  == 2853878795 ) BBDebugBreak();
        // if( x + 3  == 2853878795 ) BBDebugBreak();
        // if( x + 4  == 2853878795 ) BBDebugBreak();
        // if( x + 5  == 2853878795 ) BBDebugBreak();
        // if( x + 6  == 2853878795 ) BBDebugBreak();
        // if( x + 7  == 2853878795 ) BBDebugBreak();
        // if( x + 8  == 2853878795 ) BBDebugBreak();
        // if( x + 9  == 2853878795 ) BBDebugBreak();
        // if( x + 10 == 2853878795 ) BBDebugBreak();
        // if( x + 11 == 2853878795 ) BBDebugBreak();
        // if( x + 12 == 2853878795 ) BBDebugBreak();
        // if( x + 13 == 2853878795 ) BBDebugBreak();
        // if( x + 14 == 2853878795 ) BBDebugBreak();
        // if( x + 15 == 2853878795 ) BBDebugBreak();

        block += entriesPerBlock;
        x     += entriesPerBlock;
    }

    // Process trailing entries
    for( uint64 i = 0; i < trailingEntries; i++ )
    {
        const uint32 y   = Swap32( block[i] );
        const uint32 idx = --pfxSum[y >> kMinusKExtraBits];
        
        yBuckets[idx] = ( y  << kExtraBits ) | ( ( x + i ) >> kMinusKExtraBits );
        xBuckets[idx] = x + i;
    }
}

//-----------------------------------------------------------
template<typename TJob>
inline void F1BucketJob<TJob>::CalculateMultiThreadedPrefixSum( 
    uint32 counts[BB_DP_BUCKET_COUNT],
    uint32 pfxSum[BB_DP_BUCKET_COUNT],
    const size_t fileBlockSize
)
{
    const uint32 jobId    = this->JobId();
    const uint32 jobCount = this->JobCount();

    this->counts = counts;
    this->SyncThreads();

    // Add up all of the jobs counts
    memset( pfxSum, 0, sizeof( uint32 ) * BB_DP_BUCKET_COUNT );

    for( uint i = 0; i < jobCount; i++ )
    {
        const uint* tCounts = this->GetJob( i ).counts;

        for( uint j = 0; j < BB_DP_BUCKET_COUNT; j++ )
            pfxSum[j] += tCounts[j];
    }

    // #TODO: Only do this for the control thread?
    // uint32 totalCount = 0;
    // for( uint i = 0; i < BB_DP_BUCKET_COUNT; i++ )
    //     totalCount += pfxSum[i];

    // If we're the control thread, retain the total bucket count for this chunk.
    if( this->IsControlThread() )
    {
        uint32* totalBucketCounts = this->totalBucketCounts;

        // Add total bucket counts
        for( uint i = 0; i < BB_DP_BUCKET_COUNT; i++ )
            this->totalBucketCounts[i] += pfxSum[i];
    }

    // #TODO: Only do this if using Direct IO
    // We need to align our bucket totals to the 
    // file block size boundary so that each block buffer
    // is properly aligned for direct io.
    if( fileBlockSize )
    {
        for( uint i = 0; i < BB_DP_BUCKET_COUNT; i++ )
            pfxSum[i] = RoundUpToNextBoundary( pfxSum[i] * sizeof( uint32 ), (int)fileBlockSize ) / sizeof( uint32 );
    }

    // Calculate the prefix sum
    for( uint i = 1; i < BB_DP_BUCKET_COUNT; i++ )
        pfxSum[i] += pfxSum[i-1];

    // Subtract the count from all threads after ours 
    // to get the correct prefix sum for this thread
    for( uint t = jobId+1; t < jobCount; t++ )
    {
        const uint* tCounts = this->GetJob( t ).counts;

        for( uint i = 0; i < BB_DP_BUCKET_COUNT; i++ )
            pfxSum[i] -= tCounts[i];
    }
}