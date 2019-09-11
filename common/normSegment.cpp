#include "normSegment.h"

#include <errno.h>  // for strerror()

NormSegmentPool::NormSegmentPool()
 : seg_size(0), seg_count(0), seg_total(0), seg_list(NULL),
   peak_usage(0), overruns(0), overrun_flag(false)
{    
}

NormSegmentPool::~NormSegmentPool()
{
    Destroy();
}

bool NormSegmentPool::Init(unsigned int count, unsigned int size)
{
    if (seg_list) Destroy();
    peak_usage = 0;
    overruns = 0;    
    // This makes sure we get appropriate alignment
    unsigned int alloc_size = size / sizeof(char*);
    if ((alloc_size*sizeof(char*)) < size) alloc_size++;
    seg_size = alloc_size * sizeof(char*);
    for (unsigned int i = 0; i < count; i++)
    {
        char** ptr = new char*[alloc_size];
        if (ptr)
        {
            *ptr = seg_list;
            seg_list = (char*)ptr;
            seg_count++;
        }
        else
        {
            DMSG(0, "NormSegmentPool::Init() memory allocation error: %s\n",
                    strerror(errno));
            seg_total = seg_count;
            Destroy();
            return false;
        }
    }  
    seg_total = seg_count;
    return true;
}  // end NormSegmentPool::Init()

void NormSegmentPool::Destroy()
{
    ASSERT(seg_count == seg_total);
    char** ptr = (char**)seg_list;
    while (ptr)
    {
        char* next = *ptr;
        delete ptr;
        ptr = (char**)next;         
    }
    seg_list = NULL;
    seg_count = 0;
    seg_total = 0;
    seg_size = 0;
}  // end NormSegmentPool::Destroy()

char* NormSegmentPool::Get()
{
    char** ptr = (char**)seg_list;
    if (ptr)
    {
        seg_list = *ptr;
        seg_count--;
//#ifdef NORM_DEBUG
        overrun_flag = false;
        unsigned int usage = seg_total - seg_count;
        if (usage > peak_usage) peak_usage = usage;
    }
    else
    {
        if (!overrun_flag)
        {
            overruns++; 
            overrun_flag = true;
        } 
//#endif // NORM_DEBUG 
    }
    return ((char*)ptr);
}  // end NormSegmentPool::GetSegment()


////////////////////////////////////////////////////////////
// NormBlock Implementation

NormBlock::NormBlock()
 : size(0), segment_table(NULL), erasure_count(0), parity_count(0)
{
}     

NormBlock::~NormBlock()
{
    Destroy();
}

bool NormBlock::Init(UINT16 blockSize)
{
    if (segment_table) Destroy();
    if (!(segment_table = new char*[blockSize]))
    {
        DMSG(0, "NormBlock::Init() segment_table allocation error: %s\n", strerror(errno));
        return false;   
    }
    memset(segment_table, 0, blockSize*sizeof(char*));
    if (!pending_mask.Init(blockSize))
    {
        DMSG(0, "NormBlock::Init() pending_mask allocation error: %s\n", strerror(errno));
        Destroy();
        return false;   
    }
    if (!repair_mask.Init(blockSize))
    {
        DMSG(0, "NormBlock::Init() repair_mask allocation error: %s\n", strerror(errno));
        Destroy();
        return false;   
    }
    size = blockSize;
    erasure_count = 0;
    parity_count = 0;
    parity_offset = 0;;
    return true;
}  // end NormBlock::Init()

void NormBlock::Destroy()
{
    repair_mask.Destroy();
    pending_mask.Destroy();
    // (TBD) Option to return segments to pool from which they came
    if (segment_table)
    {
        for (unsigned int i = 0; i < size; i++)
        {
            ASSERT(!segment_table[i]);
            if (segment_table[i]) delete []segment_table[i];
        }
        delete []segment_table;
        segment_table = (char**)NULL;
    }
    erasure_count = parity_count = size = 0;
}  // end NormBlock::Destroy()

void NormBlock::EmptyToPool(NormSegmentPool& segmentPool)
{
    ASSERT(segment_table);
    for (unsigned int i = 0; i < size; i++)
    {
        if (segment_table[i]) 
        {
            segmentPool.Put(segment_table[i]);
            segment_table[i] = (char*)NULL;
        }
    }
}  // end NormBlock::EmptyToPool()

bool NormBlock::IsEmpty()
{
    ASSERT(segment_table);
    for (unsigned int i = 0; i < size; i++)
        if (segment_table[i]) return false;
    return true;
}  // end NormBlock::EmptyToPool()

// Used by client side to determine if NACK should be sent
// Note: This clears the block's "repair_mask" state
bool NormBlock::IsRepairPending(UINT16 ndata, UINT16 nparity)
{
    // Clients ask for a block of parity to fulfill their
    // repair needs (erasure_count), but if there isn't 
    // enough parity, they ask for some data segments, too
    if (erasure_count > nparity)
    {
        if (nparity)
        {
            UINT16 i = nparity;
            NormSegmentId nextId = pending_mask.FirstSet();
            while (i--)
            {
                // (TBD) for more NACK suppression, we could skip ahead
                // if this bit is already set in repair_mask?
                repair_mask.Set(nextId);  // set bit a parity can fill
                nextId++;
                nextId = pending_mask.NextSet(nextId);  
            } 
        }
        else if (size > ndata)
        {
            repair_mask.SetBits(ndata, size-ndata);   
        }  
    }
    else
    {
        repair_mask.SetBits(0, ndata);
        repair_mask.SetBits(ndata+erasure_count, nparity-erasure_count);
    }
    repair_mask.XCopy(pending_mask);
    return (repair_mask.IsSet());
}  // end NormBlock::IsRepairPending()

// Called by server
bool NormBlock::TxReset(UINT16 ndata, 
                        UINT16 nparity, 
                        UINT16 autoParity, 
                        UINT16 segmentSize)
{
    bool increasedRepair = false;
    repair_mask.SetBits(0, ndata+autoParity);
    repair_mask.UnsetBits(ndata+autoParity, nparity-autoParity);
    repair_mask.Xor(pending_mask);
    if (repair_mask.IsSet()) 
    {
        increasedRepair = true;
        repair_mask.Clear();
        pending_mask.SetBits(0, ndata+autoParity);
        pending_mask.UnsetBits(ndata+autoParity, nparity-autoParity);
        parity_offset = autoParity;  // reset parity since we're resending this one
        parity_count = nparity;      // no parity repair this repair cycle
        SetFlag(IN_REPAIR);
        if (!ParityReady(ndata))  // (TBD) only when incrementalParity == true
        {
            // Clear _any_ existing incremental parity state
            char** ptr = segment_table+ndata;
            while (nparity--)
            {
                if (*ptr) memset(*ptr, 0, segmentSize);
                ptr++;
            }
            erasure_count = 0;
        }
    }
    return increasedRepair;
}  // end NormBlock::TxReset()

bool NormBlock::ActivateRepairs(UINT16 nparity)
{
    if (repair_mask.IsSet())
    {
        pending_mask.Add(repair_mask);
        repair_mask.Clear();   
        return true;
    }
    else
    {
        return false;
    }
}  // end NormBlock::ActivateRepairs()

// For NACKs arriving during server repair_timer "holdoff" time
bool NormBlock::TxUpdate(NormSegmentId nextId, NormSegmentId lastId,
                         UINT16 ndata, UINT16 nparity, 
                         UINT16 segmentSize, UINT16 erasureCount)
{
    bool increasedRepair = false;
        
    if (nextId < ndata)
    {
        // Explicit data repair request
        parity_offset = parity_count = nparity;
        while (nextId <= lastId)
        {
            if (!pending_mask.Test(nextId))
            {
                pending_mask.Set(nextId);
                increasedRepair = true;
            }
            nextId++;      
        }
    }
    else
    {
        // parity repair request
        UINT16 parityAvailable = nparity - parity_offset;
        if (erasureCount <= parityAvailable)
        {
           // Use fresh parity for repair
           if (erasureCount > parity_count)
           {
               pending_mask.SetBits(ndata+parity_offset+parity_count, 
                                    erasureCount - parity_count);
               parity_count = erasureCount;
               increasedRepair = true; 
           }
        }
        else
        {
            // Use any remaining fresh parity ...
            if (parity_count < parityAvailable)
            {
                UINT16 count = parityAvailable - parity_count;
                pending_mask.SetBits(ndata+parity_offset+parity_count, count); 
                parity_count = parityAvailable;  
                nextId += parityAvailable;
                increasedRepair = true;
            }
            // and explicit repair for the rest
            while (nextId <= lastId)
            {
                if (!pending_mask.Test(nextId))
                {
                    pending_mask.Set(nextId);
                    increasedRepair = true;
                }
                nextId++; 
            } 
        }   
    }
    return increasedRepair;
}  // end NormBlock::TxUpdate()

bool NormBlock::HandleSegmentRequest(NormSegmentId nextId, NormSegmentId lastId,
                                     UINT16 ndata, UINT16 nparity, UINT16 erasureCount)
{
    DMSG(4, "NormBlock::HandleSegmentRequest() blk>%lu seg>%hu:%hu erasures:%hu\n",
            (UINT32)id, (UINT16)nextId, (UINT16)lastId, erasureCount);
    bool increasedRepair = false;
    if (nextId < ndata)
    {
        // Explicit data repair request
        parity_count = parity_offset = nparity;
        while (nextId <= lastId)
        {
            if (!repair_mask.Test(nextId))
            {
                repair_mask.Set(nextId);
                increasedRepair = true;
            }
            nextId++; 
        }   
    }
    else
    {
        // parity repair request
        UINT16 parityAvailable = nparity - parity_offset;
        if (erasureCount <= parityAvailable)
        {
           // Use fresh parity for repair
           if (erasureCount > parity_count)
           {
               repair_mask.SetBits(ndata+parity_offset+parity_count, 
                                   erasureCount - parity_count);
               parity_count = erasureCount;
               increasedRepair = true; 
           }
        }
        else
        {
            // Use any remaining fresh parity ...
            if (parity_count < parityAvailable)
            {
                UINT16 count = parityAvailable - parity_count;
                repair_mask.SetBits(ndata+parity_offset+parity_count, count); 
                parity_count = parityAvailable;  
                nextId += parityAvailable;
                increasedRepair = true;
            }
            // and explicit repair for the rest
            while (nextId <= lastId)
            {
                if (!repair_mask.Test(nextId))
                {
                    repair_mask.Set(nextId);
                    increasedRepair = true;
                }
                nextId++; 
            } 
        }   
    }
    return increasedRepair;
}  // end NormBlock::HandleSegmentRequest()


// Called by client
bool NormBlock::AppendRepairRequest(NormNackMsg&    nack, 
                                    UINT16          ndata, 
                                    UINT16          nparity,
                                    NormObjectId    objectId,
                                    bool            pendingInfo,
                                    UINT16          segmentSize)
{
    ASSERT(pending_mask.FirstSet() < ndata);
    NormRepairRequest req;
    nack.AttachRepairRequest(req, segmentSize);
    req.SetFlag(NormRepairRequest::SEGMENT);
    if (pendingInfo) req.SetFlag(NormRepairRequest::INFO);
    NormSegmentId nextId, lastId;
    
    if (erasure_count > nparity)
    {
        // Request explicit repair 
        nextId = pending_mask.FirstSet();
        UINT16 i = nparity;
        // Skip nparity missing data segments
        while (i--)
        {
            nextId++;
            nextId = pending_mask.NextSet(nextId);
        }
        lastId = ndata + nparity;
    }
    else
    {
        nextId = pending_mask.NextSet(ndata);
        lastId = ndata + erasure_count;   
    }
    NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
    UINT16 reqCount = 0;
    NormSegmentId prevId = nextId;
    while ((nextId <= lastId) || (reqCount > 0))
    {
        // force break of possible ending consec. series
        if (nextId == lastId) nextId++; 
        if (reqCount && (reqCount == (nextId - prevId)))
        {
            reqCount++;  // consecutive series continues
        }
        else
        {
            NormRepairRequest::Form nextForm;
            switch(reqCount)
            {
                case 0:
                    nextForm = NormRepairRequest::INVALID;
                    break;
                case 1:
                case 2:
                    nextForm = NormRepairRequest::ITEMS;
                    break;
                default:
                    nextForm = NormRepairRequest::RANGES;
                    break;
            }  // end switch(reqCount)
            if (prevForm != nextForm)
            {
                if (NormRepairRequest::INVALID != prevForm)
                    nack.PackRepairRequest(req);  // (TBD) error check
                if (NormRepairRequest::INVALID != nextForm)
                {
                    nack.AttachRepairRequest(req, segmentSize);
                    req.SetForm(nextForm);
                    req.SetFlag(NormRepairRequest::SEGMENT);
                    if (pendingInfo) req.SetFlag(NormRepairRequest::INFO);
                }
                prevForm = nextForm;
            }
            if (NormRepairRequest::INVALID != nextForm)
                DMSG(6, "NormBlock::AppendRepairRequest() SEGMENT request\n");
            switch (nextForm)
            {
                case NormRepairRequest::ITEMS:
                    req.AppendRepairItem(objectId, id, prevId);  // (TBD) error check
                    if (2 == reqCount)
                        req.AppendRepairItem(objectId, id, prevId+1); // (TBD) error check
                    break;
                case NormRepairRequest::RANGES:
                    req.AppendRepairItem(objectId, id, prevId); // (TBD) error check
                    req.AppendRepairItem(objectId, id, prevId+reqCount-1); // (TBD) error check
                    break;
                default:
                    break;
            }  // end switch(nextForm)
            prevId = nextId;
            if (nextId < lastId)
                reqCount = 1;
            else
                reqCount = 0;
        }  // end if/else (reqCount && (reqCount == (nextId - prevId)))
        nextId++;
        if (nextId <= lastId) nextId = pending_mask.NextSet(nextId);
    }  // end while(nextId <= lastId)
    if (NormRepairRequest::INVALID != prevForm)
        nack.PackRepairRequest(req);  // (TBD) error check
    return true;
}  // end NormBlock::AppendRepairRequest()
         
NormBlockPool::NormBlockPool()
 : head((NormBlock*)NULL), overruns(0), overrun_flag(false)
{
}

NormBlockPool::~NormBlockPool()
{
    Destroy();
}

bool NormBlockPool::Init(UINT32 numBlocks, UINT16 blockSize)
{
    if (head) Destroy();
    for (UINT32 i = 0; i < numBlocks; i++)
    {
        NormBlock* b = new NormBlock();
        if (b)
        {
            if (!b->Init(blockSize))
            {
                DMSG(0, "NormBlockPool::Init() block init error\n");
                delete b;
                Destroy();
                return false;   
            }  
            b->next = head;
            head = b;
        }
        else
        {
            DMSG(0, "NormBlockPool::Init() new block error\n");
            Destroy();
            return false; 
        } 
    }
    return true;
}  // end NormBlockPool::Init()

void NormBlockPool::Destroy()
{
    NormBlock* next;
    while ((next = head))
    {
        head = next->next;
        delete next;   
    }
}  // end NormBlockPool::Destroy()

NormBlockBuffer::NormBlockBuffer()
 : table((NormBlock**)NULL), range_max(0), range(0)
{
}

NormBlockBuffer::~NormBlockBuffer()
{
    Destroy();
}

bool NormBlockBuffer::Init(unsigned long rangeMax, unsigned long tableSize)
{
    if (table) Destroy();
    // Make sure tableSize is greater than 0 and 2^n
    if (!rangeMax || !tableSize) 
    {
        DMSG(0, "NormBlockBuffer::Init() bad range(%lu) or tableSize(%lu)\n",
                rangeMax, tableSize);
        return false;
    }
    if (0 != (tableSize & 0x07)) tableSize = (tableSize >> 3) + 1;
    if (!(table = new NormBlock*[tableSize]))
    {
        DMSG(0, "NormBlockBuffer::Init() buffer allocation error: %s\n", strerror(errno));
        return false;         
    }
    memset(table, 0, tableSize*sizeof(char*));
    hash_mask = tableSize - 1;
    range_max = rangeMax;
    range = 0;
    return true;
}  // end NormBlockBuffer::Init()

void NormBlockBuffer::Destroy()
{
    range_max = range = 0;  
    if (table)
    {
        NormBlock* block;
        while((block = Find(range_lo)))
        {
            DMSG(0, "NormBlockBuffer::Destroy() buffer not empty!?\n");
            Remove(block);
            delete block;   
        }
        delete []table;
        table = (NormBlock**)NULL;
        range_max = 0;
    }     
}  // end NormBlockBuffer::Destroy()

NormBlock* NormBlockBuffer::Find(const NormBlockId& blockId) const
{
    if (range)
    {
        if ((blockId < range_lo)  || (blockId > range_hi)) 
            return (NormBlock*)NULL;
        NormBlock* theBlock = table[((UINT32)blockId) & hash_mask];
        while (theBlock && (blockId != theBlock->Id())) 
            theBlock = theBlock->next;
        return theBlock;
    }
    else
    {
        return (NormBlock*)NULL;
    }   
}  // end NormBlockBuffer::Find()


bool NormBlockBuffer::CanInsert(NormBlockId blockId) const
{
    if (0 != range)
    {
        if (blockId < range_lo)
        {
            if ((range_lo - blockId + range) > range_max)
                return false;
            else
                return true;
        }
        else if (blockId > range_hi)
        {
            if ((blockId - range_hi + range) > range_max)
                return false;
            else
                return true;
        }
        else
        {
            return true;
        }        
    }
    else
    {
        return true;
    }    
}  // end NormBlockBuffer::CanInsert()


bool NormBlockBuffer::Insert(NormBlock* theBlock)
{
    const NormBlockId& blockId = theBlock->Id();
    if (!range)
    {
        range_lo = range_hi = blockId;
        range = 1;   
    }
    if (blockId < range_lo)
    {
        UINT32 newRange = range_lo - blockId + range;
        if (newRange > range_max) return false;
        range_lo = blockId;
        range = newRange;
    }
    else if (blockId > range_hi)
    {            
        UINT32 newRange = blockId - range_hi + range;
        if (newRange > range_max) return false;
        range_hi = blockId;
        range = newRange;
    }
    UINT32 index = ((UINT32)blockId) & hash_mask;
    NormBlock* prev = NULL;
    NormBlock* entry = table[index];
    while (entry && (entry->Id() < blockId)) 
    {
        prev = entry;
        entry = entry->next;
    }  
    if (prev)
        prev->next = theBlock;
    else
        table[index] = theBlock;
    ASSERT((entry ? (blockId != entry->Id()) : true));
    theBlock->next = entry;
    return true;
}  // end NormBlockBuffer::Insert()

bool NormBlockBuffer::Remove(const NormBlock* theBlock)
{
    ASSERT(theBlock);
    if (range)
    {
        const NormBlockId& blockId = theBlock->Id();
        if ((blockId < range_lo) || (blockId > range_hi)) return false;
        UINT32 index = ((UINT32)blockId) & hash_mask;
        NormBlock* prev = NULL;
        NormBlock* entry = table[index];
        while (entry && (entry->Id() != blockId))
        {
            prev = entry;
            entry = entry->next;
        }
        if (!entry) return false;
        if (prev)
            prev->next = entry->next;
        else
            table[index] = entry->next;
        
        if (range > 1)
        {
            if (blockId == range_lo)
            {
                // Find next entry for range_lo
                UINT32 i = index;
                UINT32 endex;
                if (range <= hash_mask)
                    endex = (index + range - 1) & hash_mask;
                else
                    endex = index;
                entry = NULL;
                UINT32 offset = 0;
                NormBlockId nextId = range_hi;
                do
                {
                    ++i &= hash_mask;
                    offset++;
                    if ((entry = table[i]))
                    {
                        NormBlockId id = (UINT32)index + offset;
                        while(entry && (entry->Id() != id)) 
                        {
                            if ((entry->Id() > blockId) && 
                                (entry->Id() < nextId)) nextId = entry->Id();
                            entry = entry->next;
                               
                        }
                        if (entry) break;    
                    }
                } while (i != endex);
                if (entry)
                    range_lo = entry->Id();
                else
                    range_lo = nextId;
                range = range_hi - range_lo + 1; 
            }
            else if (blockId == range_hi)
            {
                // Find prev entry for range_hi
                UINT32 i = index;
                UINT32 endex;
                if (range <= hash_mask)
                    endex = (index - range + 1) & hash_mask;
                else
                    endex = index;
                entry = NULL;
                UINT32 offset = 0;
                //printf("preving i:%lu endex:%lu lo:%lu hi:%lu\n", i, endex, (UINT32)range_lo, (UINT32) range_hi);
                NormBlockId prevId = range_lo;
                do
                {
                    --i &= hash_mask;
                    offset++;
                    if ((entry = table[i]))
                    {
                        NormBlockId id = (UINT32)index - offset;
                        //printf("Looking for id:%lu at index:%lu\n", (UINT32)id, i);
                        while(entry && (entry->Id() != id)) 
                        {
                            if ((entry->Id() < blockId) && 
                                (entry->Id() > prevId)) prevId = entry->Id();
                            entry = entry->next;
                        }
                        if (entry) break;    
                    }
                } while (i != endex);
                if (entry)
                    range_hi = entry->Id();
                else 
                    range_hi = prevId;
                range = range_hi - range_lo + 1;
            } 
        }
        else
        {
            range = 0;
        }  
        return true;
    }
    else
    {
        return false;
    }
}  // end NormBlockBuffer::Remove()

NormBlockBuffer::Iterator::Iterator(const NormBlockBuffer& blockBuffer)
 : buffer(blockBuffer), reset(true)
{
}

NormBlock* NormBlockBuffer::Iterator::GetNextBlock()
{
    if (reset)
    {
        if (buffer.range)
        {
            reset = false;
            index = buffer.range_lo;
            return buffer.Find(index);
        }
        else
        {
            return (NormBlock*)NULL;
        }
    }
    else
    {
        if (buffer.range && 
            (index < buffer.range_hi) && 
            (index >= buffer.range_lo))
        {
            // Find next entry _after_ current "index"
            UINT32 i = index;
            UINT32 endex;
            if ((UINT32)(buffer.range_hi - index) <= buffer.hash_mask)
                endex = buffer.range_hi & buffer.hash_mask;
            else
                endex = index;
            UINT32 offset = 0;
            NormBlockId nextId = buffer.range_hi;
            do
            {
                ++i &= buffer.hash_mask;
                offset++;
                NormBlockId id = (UINT32)index + offset;
                ASSERT(i < 256);
                NormBlock* entry = buffer.table[i];
                while ((NULL != entry ) && (entry->Id() != id)) 
                {
                    if ((entry->Id() > index) && (entry->Id() < nextId))
                        nextId = entry->Id();
                    entry = NormBlockBuffer::Next(entry);
                }
                if (entry)
                {
                    index = entry->Id();
                    return entry;   
                } 
            } while (i != endex);
            // If we get here, use nextId value
            index = nextId;
            return buffer.Find(nextId);
        }
        else
        {
            return (NormBlock*)NULL;
        }
    }   
}  // end NormBlockBuffer::Iterator::GetNextBlock()
