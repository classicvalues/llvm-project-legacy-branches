//===-- IRMemoryMap.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/DataBufferHeap.h"
#include "lldb/Core/DataExtractor.h"
#include "lldb/Core/Error.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/Scalar.h"
#include "lldb/Expression/IRMemoryMap.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

using namespace lldb_private;

IRMemoryMap::IRMemoryMap (lldb::TargetSP target_sp) :
    m_target_wp(target_sp)
{
    if (target_sp)
        m_process_wp = target_sp->GetProcessSP();
}

IRMemoryMap::~IRMemoryMap ()
{
    lldb::ProcessSP process_sp = m_process_wp.lock();

    if (process_sp)
    {
        AllocationMap::iterator iter;

        Error err;

        while ((iter = m_allocations.begin()) != m_allocations.end())
        {
            err.Clear();
            if (iter->second.m_leak)
                m_allocations.erase(iter);
            else
                Free(iter->first, err);
        }
    }
}

lldb::addr_t
IRMemoryMap::FindSpace (size_t size, bool zero_memory)
{
    lldb::TargetSP target_sp = m_target_wp.lock();
    lldb::ProcessSP process_sp = m_process_wp.lock();

    lldb::addr_t ret = LLDB_INVALID_ADDRESS;
    if (size == 0)
        return ret;

    if (process_sp && process_sp->CanJIT() && process_sp->IsAlive())
    {
        Error alloc_error;

        if (!zero_memory)
            ret = process_sp->AllocateMemory(size, lldb::ePermissionsReadable | lldb::ePermissionsWritable, alloc_error);
        else
            ret = process_sp->CallocateMemory(size, lldb::ePermissionsReadable | lldb::ePermissionsWritable, alloc_error);

        if (!alloc_error.Success())
            return LLDB_INVALID_ADDRESS;
        else
            return ret;
    }

    ret = 0;
    if (!m_allocations.empty())
    {
        auto back = m_allocations.rbegin();
        lldb::addr_t addr = back->first;
        size_t alloc_size = back->second.m_size;
        ret = llvm::alignTo(addr+alloc_size, 4096);
    }

    return ret;
}

IRMemoryMap::AllocationMap::iterator
IRMemoryMap::FindAllocation (lldb::addr_t addr, size_t size)
{
    if (addr == LLDB_INVALID_ADDRESS)
        return m_allocations.end();

    AllocationMap::iterator iter = m_allocations.lower_bound (addr);

    if (iter == m_allocations.end() ||
        iter->first > addr)
    {
        if (iter == m_allocations.begin())
            return m_allocations.end();
        iter--;
    }

    if (iter->first <= addr && iter->first + iter->second.m_size >= addr + size)
        return iter;

    return m_allocations.end();
}

bool
IRMemoryMap::IntersectsAllocation (lldb::addr_t addr, size_t size) const
{
    if (addr == LLDB_INVALID_ADDRESS)
        return false;

    AllocationMap::const_iterator iter = m_allocations.lower_bound (addr);

    // Since we only know that the returned interval begins at a location greater than or
    // equal to where the given interval begins, it's possible that the given interval
    // intersects either the returned interval or the previous interval.  Thus, we need to
    // check both. Note that we only need to check these two intervals.  Since all intervals
    // are disjoint it is not possible that an adjacent interval does not intersect, but a
    // non-adjacent interval does intersect.
    if (iter != m_allocations.end()) {
        if (AllocationsIntersect(addr, size, iter->second.m_process_start, iter->second.m_size))
            return true;
    }

    if (iter != m_allocations.begin()) {
        --iter;
        if (AllocationsIntersect(addr, size, iter->second.m_process_start, iter->second.m_size))
            return true;
    }

    return false;
}

bool
IRMemoryMap::AllocationsIntersect(lldb::addr_t addr1, size_t size1, lldb::addr_t addr2, size_t size2) {
  // Given two half open intervals [A, B) and [X, Y), the only 6 permutations that satisfy
  // A<B and X<Y are the following:
  // A B X Y
  // A X B Y  (intersects)
  // A X Y B  (intersects)
  // X A B Y  (intersects)
  // X A Y B  (intersects)
  // X Y A B
  // The first is B <= X, and the last is Y <= A.
  // So the condition is !(B <= X || Y <= A)), or (X < B && A < Y)
  return (addr2 < (addr1 + size1)) && (addr1 < (addr2 + size2));
}

lldb::ByteOrder
IRMemoryMap::GetByteOrder()
{
    lldb::ProcessSP process_sp = m_process_wp.lock();

    if (process_sp)
        return process_sp->GetByteOrder();

    lldb::TargetSP target_sp = m_target_wp.lock();

    if (target_sp)
        return target_sp->GetArchitecture().GetByteOrder();

    return lldb::eByteOrderInvalid;
}

uint32_t
IRMemoryMap::GetAddressByteSize()
{
    lldb::ProcessSP process_sp = m_process_wp.lock();

    if (process_sp)
        return process_sp->GetAddressByteSize();

    lldb::TargetSP target_sp = m_target_wp.lock();

    if (target_sp)
        return target_sp->GetArchitecture().GetAddressByteSize();

    return UINT32_MAX;
}

ExecutionContextScope *
IRMemoryMap::GetBestExecutionContextScope() const
{
    lldb::ProcessSP process_sp = m_process_wp.lock();

    if (process_sp)
        return process_sp.get();

    lldb::TargetSP target_sp = m_target_wp.lock();

    if (target_sp)
        return target_sp.get();

    return NULL;
}

IRMemoryMap::Allocation::Allocation (lldb::addr_t process_alloc,
                                     lldb::addr_t process_start,
                                     size_t size,
                                     uint32_t permissions,
                                     uint8_t alignment,
                                     AllocationPolicy policy) :
    m_process_alloc (process_alloc),
    m_process_start (process_start),
    m_size (size),
    m_permissions (permissions),
    m_alignment (alignment),
    m_policy (policy),
    m_leak (false)
{
    switch (policy)
    {
        default:
            assert (0 && "We cannot reach this!");
        case eAllocationPolicyHostOnly:
            m_data.SetByteSize(size);
            memset(m_data.GetBytes(), 0, size);
            break;
        case eAllocationPolicyProcessOnly:
            break;
        case eAllocationPolicyMirror:
            m_data.SetByteSize(size);
            memset(m_data.GetBytes(), 0, size);
            break;
    }
}

lldb::addr_t
IRMemoryMap::Malloc (size_t size, uint8_t alignment, uint32_t permissions, AllocationPolicy policy, bool zero_memory, Error &error)
{
    lldb_private::Log *log (lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));
    error.Clear();

    lldb::ProcessSP process_sp;
    lldb::addr_t    allocation_address  = LLDB_INVALID_ADDRESS;
    lldb::addr_t    aligned_address     = LLDB_INVALID_ADDRESS;

    size_t          alignment_mask = alignment - 1;
    size_t          allocation_size;

    if (size == 0)
        allocation_size = alignment;
    else
        allocation_size = (size & alignment_mask) ? ((size + alignment) & (~alignment_mask)) : size;

    switch (policy)
    {
    default:
        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't malloc: invalid allocation policy");
        return LLDB_INVALID_ADDRESS;
    case eAllocationPolicyHostOnly:
        allocation_address = FindSpace(allocation_size);
        if (allocation_address == LLDB_INVALID_ADDRESS)
        {
            error.SetErrorToGenericError();
            error.SetErrorString("Couldn't malloc: address space is full");
            return LLDB_INVALID_ADDRESS;
        }
        break;
    case eAllocationPolicyMirror:
        process_sp = m_process_wp.lock();
        if (log)
            log->Printf ("IRMemoryMap::%s process_sp=0x%" PRIx64 ", process_sp->CanJIT()=%s, process_sp->IsAlive()=%s", __FUNCTION__, (lldb::addr_t) process_sp.get (), process_sp && process_sp->CanJIT () ? "true" : "false", process_sp && process_sp->IsAlive () ? "true" : "false");
        if (process_sp && process_sp->CanJIT() && process_sp->IsAlive())
        {
            if (!zero_memory)
              allocation_address = process_sp->AllocateMemory(allocation_size, permissions, error);
            else
              allocation_address = process_sp->CallocateMemory(allocation_size, permissions, error);

            if (!error.Success())
                return LLDB_INVALID_ADDRESS;
        }
        else
        {
            if (log)
                log->Printf ("IRMemoryMap::%s switching to eAllocationPolicyHostOnly due to failed condition (see previous expr log message)", __FUNCTION__);
            policy = eAllocationPolicyHostOnly;
            allocation_address = FindSpace(allocation_size);
            if (allocation_address == LLDB_INVALID_ADDRESS)
            {
                error.SetErrorToGenericError();
                error.SetErrorString("Couldn't malloc: address space is full");
                return LLDB_INVALID_ADDRESS;
            }
        }
        break;
    case eAllocationPolicyProcessOnly:
        process_sp = m_process_wp.lock();
        if (process_sp)
        {
            if (process_sp->CanJIT() && process_sp->IsAlive())
            {
                if (!zero_memory)
                  allocation_address = process_sp->AllocateMemory(allocation_size, permissions, error);
                else
                  allocation_address = process_sp->CallocateMemory(allocation_size, permissions, error);

                if (!error.Success())
                    return LLDB_INVALID_ADDRESS;
            }
            else
            {
                error.SetErrorToGenericError();
                error.SetErrorString("Couldn't malloc: process doesn't support allocating memory");
                return LLDB_INVALID_ADDRESS;
            }
        }
        else
        {
            error.SetErrorToGenericError();
            error.SetErrorString("Couldn't malloc: process doesn't exist, and this memory must be in the process");
            return LLDB_INVALID_ADDRESS;
        }
        break;
    }


    lldb::addr_t mask = alignment - 1;
    aligned_address = (allocation_address + mask) & (~mask);

    m_allocations[aligned_address] = Allocation(allocation_address,
                                                aligned_address,
                                                allocation_size,
                                                permissions,
                                                alignment,
                                                policy);

    if (log)
    {
        const char * policy_string;

        switch (policy)
        {
        default:
            policy_string = "<invalid policy>";
            break;
        case eAllocationPolicyHostOnly:
            policy_string = "eAllocationPolicyHostOnly";
            break;
        case eAllocationPolicyProcessOnly:
            policy_string = "eAllocationPolicyProcessOnly";
            break;
        case eAllocationPolicyMirror:
            policy_string = "eAllocationPolicyMirror";
            break;
        }

        log->Printf("IRMemoryMap::Malloc (%" PRIu64 ", 0x%" PRIx64 ", 0x%" PRIx64 ", %s) -> 0x%" PRIx64,
                    (uint64_t)allocation_size,
                    (uint64_t)alignment,
                    (uint64_t)permissions,
                    policy_string,
                    aligned_address);
    }

    return aligned_address;
}

void
IRMemoryMap::Leak (lldb::addr_t process_address, Error &error)
{
    error.Clear();

    AllocationMap::iterator iter = m_allocations.find(process_address);

    if (iter == m_allocations.end())
    {
        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't leak: allocation doesn't exist");
        return;
    }

    Allocation &allocation = iter->second;

    allocation.m_leak = true;
}

void
IRMemoryMap::Free (lldb::addr_t process_address, Error &error)
{
    error.Clear();

    AllocationMap::iterator iter = m_allocations.find(process_address);

    if (iter == m_allocations.end())
    {
        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't free: allocation doesn't exist");
        return;
    }

    Allocation &allocation = iter->second;

    switch (allocation.m_policy)
    {
    default:
    case eAllocationPolicyHostOnly:
        {
            lldb::ProcessSP process_sp = m_process_wp.lock();
            if (process_sp)
            {
                if (process_sp->CanJIT() && process_sp->IsAlive())
                    process_sp->DeallocateMemory(allocation.m_process_alloc); // FindSpace allocated this for real
            }

            break;
        }
    case eAllocationPolicyMirror:
    case eAllocationPolicyProcessOnly:
        {
            lldb::ProcessSP process_sp = m_process_wp.lock();
            if (process_sp)
                process_sp->DeallocateMemory(allocation.m_process_alloc);
        }
    }

    if (lldb_private::Log *log = lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS))
    {
        log->Printf("IRMemoryMap::Free (0x%" PRIx64 ") freed [0x%" PRIx64 "..0x%" PRIx64 ")",
                    (uint64_t)process_address,
                    iter->second.m_process_start,
                    iter->second.m_process_start + iter->second.m_size);
    }

    m_allocations.erase(iter);
}

bool
IRMemoryMap::GetAllocSize(lldb::addr_t address, size_t &size)
{
    AllocationMap::iterator iter = FindAllocation(address, size);
    if (iter == m_allocations.end())
        return false;

    Allocation &al = iter->second;

    if (address > (al.m_process_start + al.m_size))
    {
        size = 0;
        return false;
    }

    if (address > al.m_process_start)
    {
        int dif = address - al.m_process_start;
        size = al.m_size - dif;
        return true;
    }

    size = al.m_size;
    return true;
}

void
IRMemoryMap::WriteMemory (lldb::addr_t process_address, const uint8_t *bytes, size_t size, Error &error)
{
    error.Clear();

    AllocationMap::iterator iter = FindAllocation(process_address, size);

    if (iter == m_allocations.end())
    {
        lldb::ProcessSP process_sp = m_process_wp.lock();

        if (process_sp)
        {
            process_sp->WriteMemory(process_address, bytes, size, error);
            return;
        }

        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't write: no allocation contains the target range and the process doesn't exist");
        return;
    }

    Allocation &allocation = iter->second;

    uint64_t offset = process_address - allocation.m_process_start;

    lldb::ProcessSP process_sp;

    switch (allocation.m_policy)
    {
    default:
        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't write: invalid allocation policy");
        return;
    case eAllocationPolicyHostOnly:
        if (!allocation.m_data.GetByteSize())
        {
            error.SetErrorToGenericError();
            error.SetErrorString("Couldn't write: data buffer is empty");
            return;
        }
        ::memcpy (allocation.m_data.GetBytes() + offset, bytes, size);
        break;
    case eAllocationPolicyMirror:
        if (!allocation.m_data.GetByteSize())
        {
            error.SetErrorToGenericError();
            error.SetErrorString("Couldn't write: data buffer is empty");
            return;
        }
        ::memcpy (allocation.m_data.GetBytes() + offset, bytes, size);
        process_sp = m_process_wp.lock();
        if (process_sp)
        {
            process_sp->WriteMemory(process_address, bytes, size, error);
            if (!error.Success())
                return;
        }
        break;
    case eAllocationPolicyProcessOnly:
        process_sp = m_process_wp.lock();
        if (process_sp)
        {
            process_sp->WriteMemory(process_address, bytes, size, error);
            if (!error.Success())
                return;
        }
        break;
    }

    if (lldb_private::Log *log = lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS))
    {
        log->Printf("IRMemoryMap::WriteMemory (0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRId64 ") went to [0x%" PRIx64 "..0x%" PRIx64 ")",
                    (uint64_t)process_address,
                    (uint64_t)bytes,
                    (uint64_t)size,
                    (uint64_t)allocation.m_process_start,
                    (uint64_t)allocation.m_process_start + (uint64_t)allocation.m_size);
    }
}

void
IRMemoryMap::WriteScalarToMemory (lldb::addr_t process_address, Scalar &scalar, size_t size, Error &error)
{
    error.Clear();

    if (size == UINT32_MAX)
        size = scalar.GetByteSize();

    if (size > 0)
    {
        uint8_t buf[32];
        const size_t mem_size = scalar.GetAsMemoryData (buf, size, GetByteOrder(), error);
        if (mem_size > 0)
        {
            return WriteMemory(process_address, buf, mem_size, error);
        }
        else
        {
            error.SetErrorToGenericError();
            error.SetErrorString ("Couldn't write scalar: failed to get scalar as memory data");
        }
    }
    else
    {
        error.SetErrorToGenericError();
        error.SetErrorString ("Couldn't write scalar: its size was zero");
    }
    return;
}

void
IRMemoryMap::WritePointerToMemory (lldb::addr_t process_address, lldb::addr_t address, Error &error)
{
    error.Clear();

    Scalar scalar(address);

    WriteScalarToMemory(process_address, scalar, GetAddressByteSize(), error);
}

void
IRMemoryMap::ReadMemory (uint8_t *bytes, lldb::addr_t process_address, size_t size, Error &error)
{
    error.Clear();

    AllocationMap::iterator iter = FindAllocation(process_address, size);

    if (iter == m_allocations.end())
    {
        lldb::ProcessSP process_sp = m_process_wp.lock();

        if (process_sp)
        {
            process_sp->ReadMemory(process_address, bytes, size, error);
            return;
        }

        lldb::TargetSP target_sp = m_target_wp.lock();

        if (target_sp)
        {
            Address absolute_address(process_address);
            target_sp->ReadMemory(absolute_address, false, bytes, size, error);
            return;
        }

        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't read: no allocation contains the target range, and neither the process nor the target exist");
        return;
    }

    Allocation &allocation = iter->second;

    uint64_t offset = process_address - allocation.m_process_start;

    if (offset > allocation.m_size)
    {
        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't read: data is not in the allocation");
        return;
    }

    lldb::ProcessSP process_sp;

    switch (allocation.m_policy)
    {
    default:
        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't read: invalid allocation policy");
        return;
    case eAllocationPolicyHostOnly:
        if (!allocation.m_data.GetByteSize())
        {
            error.SetErrorToGenericError();
            error.SetErrorString("Couldn't read: data buffer is empty");
            return;
        }
        if (allocation.m_data.GetByteSize() < offset + size)
        {
            error.SetErrorToGenericError();
            error.SetErrorString("Couldn't read: not enough underlying data");
            return;
        }

        ::memcpy (bytes, allocation.m_data.GetBytes() + offset, size);
        break;
    case eAllocationPolicyMirror:
        process_sp = m_process_wp.lock();
        if (process_sp)
        {
            process_sp->ReadMemory(process_address, bytes, size, error);
            if (!error.Success())
                return;
        }
        else
        {
            if (!allocation.m_data.GetByteSize())
            {
                error.SetErrorToGenericError();
                error.SetErrorString("Couldn't read: data buffer is empty");
                return;
            }
            ::memcpy (bytes, allocation.m_data.GetBytes() + offset, size);
        }
        break;
    case eAllocationPolicyProcessOnly:
        process_sp = m_process_wp.lock();
        if (process_sp)
        {
            process_sp->ReadMemory(process_address, bytes, size, error);
            if (!error.Success())
                return;
        }
        break;
    }

    if (lldb_private::Log *log = lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS))
    {
        log->Printf("IRMemoryMap::ReadMemory (0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRId64 ") came from [0x%" PRIx64 "..0x%" PRIx64 ")",
                    (uint64_t)process_address,
                    (uint64_t)bytes,
                    (uint64_t)size,
                    (uint64_t)allocation.m_process_start,
                    (uint64_t)allocation.m_process_start + (uint64_t)allocation.m_size);
    }
}

void
IRMemoryMap::ReadScalarFromMemory (Scalar &scalar, lldb::addr_t process_address, size_t size, Error &error)
{
    error.Clear();

    if (size > 0)
    {
        DataBufferHeap buf(size, 0);
        ReadMemory(buf.GetBytes(), process_address, size, error);

        if (!error.Success())
            return;

        DataExtractor extractor(buf.GetBytes(), buf.GetByteSize(), GetByteOrder(), GetAddressByteSize());

        lldb::offset_t offset = 0;

        switch (size)
        {
        default:
            error.SetErrorToGenericError();
            error.SetErrorStringWithFormat("Couldn't read scalar: unsupported size %" PRIu64, (uint64_t)size);
            return;
        case 1: scalar = extractor.GetU8(&offset);  break;
        case 2: scalar = extractor.GetU16(&offset); break;
        case 4: scalar = extractor.GetU32(&offset); break;
        case 8: scalar = extractor.GetU64(&offset); break;
        }
    }
    else
    {
        error.SetErrorToGenericError();
        error.SetErrorString ("Couldn't read scalar: its size was zero");
    }
    return;
}

void
IRMemoryMap::ReadPointerFromMemory (lldb::addr_t *address, lldb::addr_t process_address, Error &error)
{
    error.Clear();

    Scalar pointer_scalar;
    ReadScalarFromMemory(pointer_scalar, process_address, GetAddressByteSize(), error);

    if (!error.Success())
        return;

    *address = pointer_scalar.ULongLong();

    return;
}

void
IRMemoryMap::GetMemoryData (DataExtractor &extractor, lldb::addr_t process_address, size_t size, Error &error)
{
    error.Clear();

    if (size > 0)
    {
        AllocationMap::iterator iter = FindAllocation(process_address, size);

        if (iter == m_allocations.end())
        {
            error.SetErrorToGenericError();
            error.SetErrorStringWithFormat("Couldn't find an allocation containing [0x%" PRIx64 "..0x%" PRIx64 ")", process_address, process_address + size);
            return;
        }

        Allocation &allocation = iter->second;

        switch (allocation.m_policy)
        {
        default:
            error.SetErrorToGenericError();
            error.SetErrorString("Couldn't get memory data: invalid allocation policy");
            return;
        case eAllocationPolicyProcessOnly:
            error.SetErrorToGenericError();
            error.SetErrorString("Couldn't get memory data: memory is only in the target");
            return;
        case eAllocationPolicyMirror:
            {
                lldb::ProcessSP process_sp = m_process_wp.lock();

                if (!allocation.m_data.GetByteSize())
                {
                    error.SetErrorToGenericError();
                    error.SetErrorString("Couldn't get memory data: data buffer is empty");
                    return;
                }
                if (process_sp)
                {
                    process_sp->ReadMemory(allocation.m_process_start, allocation.m_data.GetBytes(), allocation.m_data.GetByteSize(), error);
                    if (!error.Success())
                        return;
                    uint64_t offset = process_address - allocation.m_process_start;
                    extractor = DataExtractor(allocation.m_data.GetBytes() + offset, size, GetByteOrder(), GetAddressByteSize());
                    return;
                }
            }
            break;
        case eAllocationPolicyHostOnly:
            if (!allocation.m_data.GetByteSize())
            {
                error.SetErrorToGenericError();
                error.SetErrorString("Couldn't get memory data: data buffer is empty");
                return;
            }
            uint64_t offset = process_address - allocation.m_process_start;
            extractor = DataExtractor(allocation.m_data.GetBytes() + offset, size, GetByteOrder(), GetAddressByteSize());
            return;
        }
    }
    else
    {
        error.SetErrorToGenericError();
        error.SetErrorString ("Couldn't get memory data: its size was zero");
        return;
    }
}
