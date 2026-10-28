// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "gcinternal.h"

#ifdef SERVER_GC
namespace SVR
{
#else // SERVER_GC
namespace WKS
{
#endif // SERVER_GC

void gc_heap::add_to_history_per_heap()
{
#if defined(GC_HISTORY) && defined(BACKGROUND_GC)
    gc_history* current_hist = &gchist_per_heap[gchist_index_per_heap];
    current_hist->gc_index = settings.gc_index;
    current_hist->current_bgc_state = current_bgc_state;
    size_t elapsed = dd_gc_elapsed_time (dynamic_data_of (0));
    current_hist->gc_time_ms = (uint32_t)(elapsed / 1000);
    current_hist->gc_efficiency = (elapsed ? (total_promoted_bytes / elapsed) : total_promoted_bytes);
#ifndef USE_REGIONS
    current_hist->eph_low = generation_allocation_start (generation_of (max_generation - 1));
    current_hist->gen0_start = generation_allocation_start (generation_of (0));
    current_hist->eph_high = heap_segment_allocated (ephemeral_heap_segment);
#endif //!USE_REGIONS
#ifdef BACKGROUND_GC
    current_hist->bgc_lowest = background_saved_lowest_address;
    current_hist->bgc_highest = background_saved_highest_address;
#endif //BACKGROUND_GC
    current_hist->fgc_lowest = lowest_address;
    current_hist->fgc_highest = highest_address;
    current_hist->g_lowest = g_gc_lowest_address;
    current_hist->g_highest = g_gc_highest_address;

    gchist_index_per_heap++;
    if (gchist_index_per_heap == max_history_count)
    {
        gchist_index_per_heap = 0;
    }
#endif //GC_HISTORY && BACKGROUND_GC
}

void gc_heap::add_to_history()
{
#if defined(GC_HISTORY) && defined(BACKGROUND_GC)
    gc_mechanisms_store* current_settings = &gchist[gchist_index];
    current_settings->store (&settings);

    gchist_index++;
    if (gchist_index == max_history_count)
    {
        gchist_index = 0;
    }
#endif //GC_HISTORY && BACKGROUND_GC
}

void gc_heap::fire_per_heap_hist_event (gc_history_per_heap* current_gc_data_per_heap, int heap_num)
{
    maxgen_size_increase* maxgen_size_info = &(current_gc_data_per_heap->maxgen_size_info);
    FIRE_EVENT(GCPerHeapHistory_V3,
               (void *)(maxgen_size_info->free_list_allocated),
               (void *)(maxgen_size_info->free_list_rejected),
               (void *)(maxgen_size_info->end_seg_allocated),
               (void *)(maxgen_size_info->condemned_allocated),
               (void *)(maxgen_size_info->pinned_allocated),
               (void *)(maxgen_size_info->pinned_allocated_advance),
               maxgen_size_info->running_free_list_efficiency,
               current_gc_data_per_heap->gen_to_condemn_reasons.get_reasons0(),
               current_gc_data_per_heap->gen_to_condemn_reasons.get_reasons1(),
               current_gc_data_per_heap->mechanisms[gc_heap_compact],
               current_gc_data_per_heap->mechanisms[gc_heap_expand],
               current_gc_data_per_heap->heap_index,
               (void *)(current_gc_data_per_heap->extra_gen0_committed),
               total_generation_count,
               (uint32_t)(sizeof (gc_generation_data)),
               (void *)&(current_gc_data_per_heap->gen_data[0]));

    current_gc_data_per_heap->print();
    current_gc_data_per_heap->gen_to_condemn_reasons.print (heap_num);
}

void gc_heap::fire_pevents()
{
    gc_history_global* current_gc_data_global = get_gc_data_global();

    settings.record (current_gc_data_global);
    current_gc_data_global->print();

#ifdef FEATURE_EVENT_TRACE
    if (!informational_event_enabled_p) return;

    uint32_t count_time_info = (settings.concurrent ? max_bgc_time_type :
                                (settings.compaction ? max_compact_time_type : max_sweep_time_type));

#ifdef BACKGROUND_GC
    uint64_t* time_info = (settings.concurrent ? bgc_time_info : gc_time_info);
#else
    uint64_t* time_info = gc_time_info;
#endif //BACKGROUND_GC
    // We don't want to have to fire the time info as 64-bit integers as there's no need to
    // so compress them down to 32-bit ones.
    uint32_t* time_info_32 = (uint32_t*)time_info;
    for (uint32_t i = 0; i < count_time_info; i++)
    {
        time_info_32[i] = limit_time_to_uint32 (time_info[i]);
    }

    FIRE_EVENT(GCGlobalHeapHistory_V4,
               current_gc_data_global->final_youngest_desired,
               current_gc_data_global->num_heaps,
               current_gc_data_global->condemned_generation,
               current_gc_data_global->gen0_reduction_count,
               current_gc_data_global->reason,
               current_gc_data_global->global_mechanisms_p,
               current_gc_data_global->pause_mode,
               current_gc_data_global->mem_pressure,
               current_gc_data_global->gen_to_condemn_reasons.get_reasons0(),
               current_gc_data_global->gen_to_condemn_reasons.get_reasons1(),
               count_time_info,
               (uint32_t)(sizeof (uint32_t)),
               (void*)time_info_32);

#ifdef MULTIPLE_HEAPS
    for (int i = 0; i < gc_heap::n_heaps; i++)
    {
        gc_heap* hp = gc_heap::g_heaps[i];
        gc_history_per_heap* current_gc_data_per_heap = hp->get_gc_data_per_heap();
        fire_per_heap_hist_event (current_gc_data_per_heap, hp->heap_number);
    }
#else
    gc_history_per_heap* current_gc_data_per_heap = get_gc_data_per_heap();
    fire_per_heap_hist_event (current_gc_data_per_heap, heap_number);
#endif //MULTIPLE_HEAPS

#ifdef FEATURE_LOH_COMPACTION
    if (!settings.concurrent && settings.loh_compaction)
    {
        // Not every heap will compact LOH, the ones that didn't will just have 0s
        // in its info.
        FIRE_EVENT(GCLOHCompact,
                   (uint16_t)get_num_heaps(),
                   (uint32_t)(sizeof (etw_loh_compact_info)),
                   (void *)loh_compact_info);
    }
#endif //FEATURE_LOH_COMPACTION
#endif //FEATURE_EVENT_TRACE
}

// This fires the amount of total committed in use, in free and on the decommit list.
// It's fired on entry and exit of each blocking GC and on entry of each BGC (not firing this on exit of a GC
// because EE is not suspended then. On entry it's fired after the GCStart event, on exit it's fire before the GCStop event.
void gc_heap::fire_committed_usage_event()
{
#ifdef FEATURE_EVENT_TRACE
    if (!EVENT_ENABLED (GCMarkWithType)) return;

    size_t total_committed = 0;
    size_t committed_decommit = 0;
    size_t committed_free = 0;
    size_t committed_bookkeeping = 0;
    size_t new_current_total_committed;
    size_t new_current_total_committed_bookkeeping;
    size_t new_committed_by_oh[recorded_committed_bucket_counts];
    compute_committed_bytes(total_committed, committed_decommit, committed_free,
                            committed_bookkeeping, new_current_total_committed, new_current_total_committed_bookkeeping,
                            new_committed_by_oh);

    size_t total_committed_in_use = new_committed_by_oh[soh] + new_committed_by_oh[loh] + new_committed_by_oh[poh];
#ifdef USE_REGIONS
    size_t total_committed_in_global_decommit = committed_decommit;
    size_t total_committed_in_free = committed_free;
    size_t total_committed_in_global_free = new_committed_by_oh[recorded_committed_free_bucket] - total_committed_in_free - total_committed_in_global_decommit;
#else
    assert (committed_decommit == 0);
    assert (committed_free == 0);
    size_t total_committed_in_global_decommit = 0;
    size_t total_committed_in_free = 0;
    size_t total_committed_in_global_free = 0;
    // For segments, bookkeeping committed does not include mark array
#endif //USE_REGIONS
    size_t total_bookkeeping_committed = committed_bookkeeping;

    GCEventFireCommittedUsage_V1 (
        (uint64_t)total_committed_in_use,
        (uint64_t)total_committed_in_global_decommit,
        (uint64_t)total_committed_in_free,
        (uint64_t)total_committed_in_global_free,
        (uint64_t)total_bookkeeping_committed
    );
#endif //FEATURE_EVENT_TRACE
}

#ifdef BACKGROUND_GC
inline
void fire_alloc_wait_event (alloc_wait_reason awr, BOOL begin_p)
{
    if (awr != awr_ignored)
    {
        if (begin_p)
        {
            FIRE_EVENT(BGCAllocWaitBegin, awr);
        }
        else
        {
            FIRE_EVENT(BGCAllocWaitEnd, awr);
        }
    }
}

void gc_heap::fire_alloc_wait_event_begin (alloc_wait_reason awr)
{
    fire_alloc_wait_event (awr, TRUE);
}

void gc_heap::fire_alloc_wait_event_end (alloc_wait_reason awr)
{
    fire_alloc_wait_event (awr, FALSE);
}

#endif //BACKGROUND_GC

void gc_heap::add_saved_spinlock_info (
            bool loh_p,
            msl_enter_state enter_state,
            msl_take_state take_state,
            enter_msl_status msl_status)
{
#ifdef SPINLOCK_HISTORY
    if (!loh_p || (msl_status == msl_retry_different_heap))
    {
        return;
    }

    spinlock_info* current = &last_spinlock_info[spinlock_info_index];

    current->enter_state = enter_state;
    current->take_state = take_state;
    current->current_uoh_alloc_state = current_uoh_alloc_state;
    current->thread_id.SetToCurrentThread();
    current->loh_p = loh_p;
    dprintf (SPINLOCK_LOG, ("[%d]%s %s %s",
        heap_number,
        (loh_p ? "loh" : "soh"),
        ((enter_state == me_acquire) ? "E" : "L"),
        msl_take_state_str[take_state]));

    spinlock_info_index++;

    assert (spinlock_info_index <= max_saved_spinlock_info);

    if (spinlock_info_index >= max_saved_spinlock_info)
    {
        spinlock_info_index = 0;
    }
#else
    UNREFERENCED_PARAMETER(enter_state);
    UNREFERENCED_PARAMETER(take_state);
#endif //SPINLOCK_HISTORY
}

#ifdef USE_REGIONS
void gc_heap::verify_region_to_generation_map()
{
#ifdef _DEBUG
    uint8_t* local_ephemeral_low = MAX_PTR;
    uint8_t* local_ephemeral_high = nullptr;
    for (int gen_number = soh_gen0; gen_number < total_generation_count; gen_number++)
    {
#ifdef MULTIPLE_HEAPS
        for (int i = 0; i < n_heaps; i++)
        {
            gc_heap* hp = g_heaps[i];
#else //MULTIPLE_HEAPS
        {
            gc_heap* hp = pGenGCHeap;
#endif //MULTIPLE_HEAPS
            generation *gen = hp->generation_of (gen_number);
            for (heap_segment *region = generation_start_segment (gen); region != nullptr; region = heap_segment_next (region))
            {
                if (heap_segment_read_only_p (region))
                {
                    // the region to generation map doesn't cover read only segments
                    continue;
                }
                size_t region_index_start = get_basic_region_index_for_address (get_region_start (region));
                size_t region_index_end = get_basic_region_index_for_address (heap_segment_reserved (region));
                int gen_num = min (gen_number, (int)soh_gen2);
                assert (gen_num == heap_segment_gen_num (region));
                int plan_gen_num = heap_segment_plan_gen_num (region);
                bool is_demoted = (region->flags & heap_segment_flags_demoted) != 0;
                bool is_sweep_in_plan = heap_segment_swept_in_plan (region);
                for (size_t region_index = region_index_start; region_index < region_index_end; region_index++)
                {
                    region_info region_info_bits = map_region_to_generation[region_index];
                    assert ((region_info_bits & RI_GEN_MASK) == gen_num);
                    assert ((region_info_bits >> RI_PLAN_GEN_SHR) == plan_gen_num);
                    assert (((region_info_bits & RI_SIP) != 0) == is_sweep_in_plan);
                    assert (((region_info_bits & RI_DEMOTED) != 0) == is_demoted);
                }
            }
        }
    }
#endif //_DEBUG
}

#endif //USE_REGIONS
#ifdef BACKGROUND_GC
void gc_heap::verify_mark_array_cleared (uint8_t* begin, uint8_t* end, uint32_t* mark_array_addr)
{
#ifdef _DEBUG
    size_t  markw = mark_word_of (begin);
    size_t  markw_end = mark_word_of (end);

    while (markw < markw_end)
    {
        if (mark_array_addr[markw])
        {
            uint8_t* addr = mark_word_address (markw);
#ifdef USE_REGIONS
            heap_segment* region = region_of (addr);
            dprintf (1, ("The mark bits at 0x%zx:0x%x(addr: 0x%p, r: %zx(%p)) were not cleared",
                            markw, mark_array_addr[markw], addr,
                            (size_t)region, heap_segment_mem (region)));
#else
            dprintf (1, ("The mark bits at 0x%zx:0x%x(addr: 0x%p) were not cleared",
                            markw, mark_array_addr[markw], addr));
#endif //USE_REGIONS
            FATAL_GC_ERROR();
        }
        markw++;
    }
#else // _DEBUG
    UNREFERENCED_PARAMETER(begin);
    UNREFERENCED_PARAMETER(end);
    UNREFERENCED_PARAMETER(mark_array_addr);
#endif //_DEBUG
}

void gc_heap::set_mem_verify (uint8_t* start, uint8_t* end, uint8_t b)
{
#ifdef VERIFY_HEAP
    if (end > start)
    {
        if ((GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_GC) &&
           !(GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_NO_MEM_FILL))
        {
            dprintf (3, ("setting mem to %c [%p, [%p", b, start, end));
            memset (start, b, (end - start));
        }
    }
#endif //VERIFY_HEAP
}

#endif //BACKGROUND_GC

void gc_heap::descr_generations_to_profiler (gen_walk_fn fn, void *context)
{
#ifdef MULTIPLE_HEAPS
    for (int i = 0; i < n_heaps; i++)
    {
        gc_heap* hp = g_heaps[i];
#else //MULTIPLE_HEAPS
    {
        gc_heap* hp = NULL;
#endif //MULTIPLE_HEAPS

        for (int curr_gen_number = total_generation_count-1; curr_gen_number >= 0; curr_gen_number--)
        {
            generation* gen = hp->generation_of (curr_gen_number);
            heap_segment* seg = heap_segment_rw (generation_start_segment (gen));
#ifdef USE_REGIONS
            while (seg)
            {
                fn(context, curr_gen_number, heap_segment_mem (seg),
                                              heap_segment_allocated (seg),
                                              heap_segment_reserved (seg));

                seg = heap_segment_next_rw (seg);
            }
#else
            while (seg && (seg != hp->ephemeral_heap_segment))
            {
                assert (curr_gen_number > 0);

                // report bounds from heap_segment_mem (seg) to
                // heap_segment_allocated (seg);
                // for generation # curr_gen_number
                // for heap # heap_no
                fn(context, curr_gen_number, heap_segment_mem (seg),
                                              heap_segment_allocated (seg),
                                              (curr_gen_number > max_generation) ?
                                                heap_segment_reserved (seg) : heap_segment_allocated (seg));

                seg = heap_segment_next_rw (seg);
            }

            if (seg)
            {
                assert (seg == hp->ephemeral_heap_segment);
                assert (curr_gen_number <= max_generation);

                if (curr_gen_number == max_generation)
                {
                    if (heap_segment_mem (seg) < generation_allocation_start (hp->generation_of (max_generation-1)))
                    {
                        // report bounds from heap_segment_mem (seg) to
                        // generation_allocation_start (generation_of (max_generation-1))
                        // for heap # heap_number
                        fn(context, curr_gen_number, heap_segment_mem (seg),
                                                      generation_allocation_start (hp->generation_of (max_generation-1)),
                                                      generation_allocation_start (hp->generation_of (max_generation-1)) );
                    }
                }
                else if (curr_gen_number != 0)
                {
                    //report bounds from generation_allocation_start (generation_of (curr_gen_number))
                    // to generation_allocation_start (generation_of (curr_gen_number-1))
                    // for heap # heap_number
                    fn(context, curr_gen_number, generation_allocation_start (hp->generation_of (curr_gen_number)),
                                                  generation_allocation_start (hp->generation_of (curr_gen_number-1)),
                                                  generation_allocation_start (hp->generation_of (curr_gen_number-1)));
                }
                else
                {
                    //report bounds from generation_allocation_start (generation_of (curr_gen_number))
                    // to heap_segment_allocated (ephemeral_heap_segment);
                    // for heap # heap_number
                    fn(context, curr_gen_number, generation_allocation_start (hp->generation_of (curr_gen_number)),
                                                  heap_segment_allocated (hp->ephemeral_heap_segment),
                                                  heap_segment_reserved (hp->ephemeral_heap_segment) );
                }
            }
#endif //USE_REGIONS
        }
    }
}

#ifdef TRACE_GC
// Note that when logging is on it can take a long time to go through the free items.
void gc_heap::print_free_list (int gen, heap_segment* seg)
{
    UNREFERENCED_PARAMETER(gen);
    UNREFERENCED_PARAMETER(seg);
/*
    if (settings.concurrent == FALSE)
    {
        uint8_t* seg_start = heap_segment_mem (seg);
        uint8_t* seg_end = heap_segment_allocated (seg);

        dprintf (3, ("Free list in seg %zx:", seg_start));

        size_t total_free_item = 0;

        allocator* gen_allocator = generation_allocator (generation_of (gen));
        for (unsigned int b = 0; b < gen_allocator->number_of_buckets(); b++)
        {
            uint8_t* fo = gen_allocator->alloc_list_head_of (b);
            while (fo)
            {
                if (fo >= seg_start && fo < seg_end)
                {
                    total_free_item++;

                    size_t free_item_len = size(fo);

                    dprintf (3, ("[%zx, %zx[:%zd",
                                 (size_t)fo,
                                 (size_t)(fo + free_item_len),
                                 free_item_len));
                }

                fo = free_list_slot (fo);
            }
        }

        dprintf (3, ("total %zd free items", total_free_item));
    }
*/
}

#endif //TRACE_GC

void gc_heap::descr_generations (const char* msg)
{
#ifndef TRACE_GC
    UNREFERENCED_PARAMETER(msg);
#endif //!TRACE_GC

#ifdef STRESS_LOG
    if (StressLog::StressLogOn(LF_GC, LL_INFO1000))
    {
        gc_heap* hp = 0;
#ifdef MULTIPLE_HEAPS
        hp= this;
#endif //MULTIPLE_HEAPS

        STRESS_LOG1(LF_GC, LL_INFO1000, "GC Heap %p\n", hp);
        for (int n = max_generation; n >= 0; --n)
        {
#ifndef USE_REGIONS
            STRESS_LOG4(LF_GC, LL_INFO1000, "    Generation %d [%p, %p] cur = %p\n",
                    n,
                    generation_allocation_start(generation_of(n)),
                    generation_allocation_limit(generation_of(n)),
                    generation_allocation_pointer(generation_of(n)));
#endif //USE_REGIONS

            heap_segment* seg = generation_start_segment(generation_of(n));
            while (seg)
            {
                STRESS_LOG4(LF_GC, LL_INFO1000, "        Segment mem %p alloc = %p used %p committed %p\n",
                        heap_segment_mem(seg),
                        heap_segment_allocated(seg),
                        heap_segment_used(seg),
                        heap_segment_committed(seg));
                seg = heap_segment_next(seg);
            }
        }
    }
#endif  // STRESS_LOG

#ifdef TRACE_GC
    dprintf (2, ("lowest_address: %zx highest_address: %zx",
             (size_t) lowest_address, (size_t) highest_address));
#ifdef BACKGROUND_GC
    dprintf (2, ("bgc lowest_address: %zx bgc highest_address: %zx",
             (size_t) background_saved_lowest_address, (size_t) background_saved_highest_address));
#endif //BACKGROUND_GC

    if (heap_number == 0)
    {
#ifdef USE_REGIONS
        size_t alloc_size = get_total_heap_size () / 1024 / 1024;
        size_t commit_size = get_total_committed_size () / 1024 / 1024;
        size_t frag_size = get_total_fragmentation () / 1024 / 1024;
        int total_new_gen0_regions_in_plns = get_total_new_gen0_regions_in_plns ();
        int total_new_regions_in_prr = get_total_new_regions_in_prr ();
        int total_new_regions_in_threading = get_total_new_regions_in_threading ();
        uint64_t elapsed_time_so_far = GetHighPrecisionTimeStamp () - process_start_time;

        size_t idx = VolatileLoadWithoutBarrier (&settings.gc_index);

        dprintf (REGIONS_LOG, ("[%s] GC#%5zd [%s] heap %zdmb (F: %zdmb %d%%) commit size: %zdmb, %0.3f min, %d,%d new in plan, %d in threading",
            msg, idx, (settings.promotion ? "PM" : "NPM"), alloc_size, frag_size,
            (int)((double)frag_size * 100.0 / (double)alloc_size),
            commit_size,
            (double)elapsed_time_so_far / (double)1000000 / (double)60,
            total_new_gen0_regions_in_plns, total_new_regions_in_prr, total_new_regions_in_threading));

#if defined(BGC_SERVO_TUNING) && defined(SIMPLE_DPRINTF)
        size_t total_gen_size_mb[loh_generation + 1] = { 0, 0, 0, 0 };
        size_t total_gen_fragmentation_mb[loh_generation + 1] = { 0, 0, 0, 0 };
        for (int i = 0; i < (loh_generation + 1); i++)
        {
            total_gen_size_mb[i] = get_total_generation_size (i) / 1024 / 1024;
            total_gen_fragmentation_mb[i] = get_total_gen_fragmentation (i) / 1024 / 1024;
        }

        int bgcs = VolatileLoadWithoutBarrier (&current_bgc_state);
        dprintf (REGIONS_LOG, ("[%s] GC#%zd (bgcs: %d, %s) g0: %zdmb (f: %zdmb %d%%), g1: %zdmb (f: %zdmb %d%%), g2: %zdmb (f: %zdmb %d%%), g3: %zdmb (f: %zdmb %d%%)",
            msg, idx, bgcs, str_bgc_state[bgcs],
            total_gen_size_mb[0], total_gen_fragmentation_mb[0], (total_gen_size_mb[0] ? (int)((double)total_gen_fragmentation_mb[0] * 100.0 / (double)total_gen_size_mb[0]) : 0),
            total_gen_size_mb[1], total_gen_fragmentation_mb[1], (total_gen_size_mb[1] ? (int)((double)total_gen_fragmentation_mb[1] * 100.0 / (double)total_gen_size_mb[1]) : 0),
            total_gen_size_mb[2], total_gen_fragmentation_mb[2], (total_gen_size_mb[2] ? (int)((double)total_gen_fragmentation_mb[2] * 100.0 / (double)total_gen_size_mb[2]) : 0),
            total_gen_size_mb[3], total_gen_fragmentation_mb[3], (total_gen_size_mb[3] ? (int)((double)total_gen_fragmentation_mb[3] * 100.0 / (double)total_gen_size_mb[3]) : 0)));
#endif //BGC_SERVO_TUNING && SIMPLE_DPRINTF
        // print every 20 GCs so it's easy to see if we are making progress.
        if ((idx % 20) == 0)
        {
            dprintf (1, ("[%5s] GC#%5zd total heap size: %zdmb (F: %zdmb %d%%) commit size: %zdmb, %0.3f min, %d,%d new in plan, %d in threading\n",
                msg, idx, alloc_size, frag_size,
                (int)((double)frag_size * 100.0 / (double)alloc_size),
                commit_size,
                (double)elapsed_time_so_far / (double)1000000 / (double)60,
                total_new_gen0_regions_in_plns, total_new_regions_in_prr, total_new_regions_in_threading));
        }
#endif //USE_REGIONS
    }

    for (int curr_gen_number = total_generation_count - 1; curr_gen_number >= 0; curr_gen_number--)
    {
        size_t total_gen_size = generation_size (curr_gen_number);
#ifdef SIMPLE_DPRINTF
        dprintf (GTC_LOG, ("[%s][g%d]gen %d:, size: %zd, frag: %zd(L: %zd, O: %zd), f: %d%% %s %s %s",
                      msg,
                      settings.condemned_generation,
                      curr_gen_number,
                      total_gen_size,
                      dd_fragmentation (dynamic_data_of (curr_gen_number)),
                      generation_free_list_space (generation_of (curr_gen_number)),
                      generation_free_obj_space (generation_of (curr_gen_number)),
                      (total_gen_size ?
                        (int)(((double)dd_fragmentation (dynamic_data_of (curr_gen_number)) / (double)total_gen_size) * 100) :
                        0),
                      (settings.compaction ? "(compact)" : "(sweep)"),
                      (settings.heap_expansion ? "(EX)" : " "),
                      (settings.promotion ? "Promotion" : "NoPromotion")));
#else
        dprintf (2, ( "Generation %d: generation size: %zd, fragmentation: %zd",
                      curr_gen_number,
                      total_gen_size,
                      dd_fragmentation (dynamic_data_of (curr_gen_number))));
#endif //SIMPLE_DPRINTF

        generation* gen = generation_of (curr_gen_number);
        heap_segment* seg = heap_segment_rw (generation_start_segment (gen));
#ifdef USE_REGIONS
        dprintf (GTC_LOG, ("g%d: start seg: %p alloc seg: %p, tail region: %p",
            curr_gen_number,
            heap_segment_mem (seg),
            (generation_allocation_segment (gen) ? heap_segment_mem (generation_allocation_segment (gen)) : 0),
            heap_segment_mem (generation_tail_region (gen))));
        while (seg)
        {
            dprintf (GTC_LOG, ("g%d: (%d:p %d) [%zx %zx(sa: %zx, pa: %zx)[-%zx[ (%zd) (%zd)",
                               curr_gen_number,
                               heap_segment_gen_num (seg),
                               heap_segment_plan_gen_num (seg),
                               (size_t)heap_segment_mem (seg),
                               (size_t)heap_segment_allocated (seg),
                               (size_t)heap_segment_saved_allocated (seg),
                               (size_t)heap_segment_plan_allocated (seg),
                               (size_t)heap_segment_committed (seg),
                               (size_t)(heap_segment_allocated (seg) - heap_segment_mem (seg)),
                               (size_t)(heap_segment_committed (seg) - heap_segment_allocated (seg))));
            print_free_list (curr_gen_number, seg);
            seg = heap_segment_next (seg);
        }
#else
        while (seg && (seg != ephemeral_heap_segment))
        {
            dprintf (GTC_LOG, ("g%d: [%zx %zx[-%zx[ (%zd) (%zd)",
                        curr_gen_number,
                        (size_t)heap_segment_mem (seg),
                        (size_t)heap_segment_allocated (seg),
                        (size_t)heap_segment_committed (seg),
                        (size_t)(heap_segment_allocated (seg) - heap_segment_mem (seg)),
                        (size_t)(heap_segment_committed (seg) - heap_segment_allocated (seg))));
            print_free_list (curr_gen_number, seg);
            seg = heap_segment_next (seg);
        }
        if (seg && (seg != generation_start_segment (gen)))
        {
            dprintf (GTC_LOG, ("g%d: [%zx %zx[",
                         curr_gen_number,
                         (size_t)heap_segment_mem (seg),
                         (size_t)generation_allocation_start (generation_of (curr_gen_number-1))));
            print_free_list (curr_gen_number, seg);

        }
        else if (seg)
        {
            dprintf (GTC_LOG, ("g%d: [%zx %zx[",
                         curr_gen_number,
                         (size_t)generation_allocation_start (generation_of (curr_gen_number)),
                         (size_t)(((curr_gen_number == 0)) ?
                                  (heap_segment_allocated
                                   (generation_start_segment
                                    (generation_of (curr_gen_number)))) :
                                  (generation_allocation_start
                                   (generation_of (curr_gen_number - 1))))
                         ));
            print_free_list (curr_gen_number, seg);
        }
#endif //USE_REGIONS
    }

#endif //TRACE_GC
}

#ifdef BACKGROUND_GC
BOOL gc_heap::bgc_mark_array_range (heap_segment* seg,
                                    BOOL whole_seg_p,
                                    uint8_t** range_beg,
                                    uint8_t** range_end)
{
    uint8_t* seg_start = heap_segment_mem (seg);
    uint8_t* seg_end = (whole_seg_p ? heap_segment_reserved (seg) : align_on_mark_word (heap_segment_allocated (seg)));

    if ((seg_start < background_saved_highest_address) &&
        (seg_end > background_saved_lowest_address))
    {
        *range_beg = max (seg_start, background_saved_lowest_address);
        *range_end = min (seg_end, background_saved_highest_address);
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

void gc_heap::bgc_verify_mark_array_cleared (heap_segment* seg, bool always_verify_p)
{
#ifdef _DEBUG
    if (gc_heap::background_running_p() || always_verify_p)
    {
        uint8_t* range_beg = 0;
        uint8_t* range_end = 0;

        if (bgc_mark_array_range (seg, TRUE, &range_beg, &range_end) || always_verify_p)
        {
            if (always_verify_p)
            {
                range_beg = heap_segment_mem (seg);
                range_end = heap_segment_reserved (seg);
            }
            size_t  markw = mark_word_of (range_beg);
            size_t  markw_end = mark_word_of (range_end);
            while (markw < markw_end)
            {
                if (mark_array [markw])
                {
                    dprintf (1, ("The mark bits at 0x%zx:0x%u(addr: 0x%p) were not cleared",
                                    markw, mark_array [markw], mark_word_address (markw)));
                    FATAL_GC_ERROR();
                }
                markw++;
            }
            uint8_t* p = mark_word_address (markw_end);
            while (p < range_end)
            {
                assert (!(mark_array_marked (p)));
                p++;
            }
        }
    }
#endif //_DEBUG
}

void gc_heap::verify_mark_bits_cleared (uint8_t* obj, size_t s)
{
#ifdef VERIFY_HEAP
    size_t start_mark_bit = mark_bit_of (obj) + 1;
    size_t end_mark_bit = mark_bit_of (obj + s);
    unsigned int startbit = mark_bit_bit (start_mark_bit);
    unsigned int endbit = mark_bit_bit (end_mark_bit);
    size_t startwrd = mark_bit_word (start_mark_bit);
    size_t endwrd = mark_bit_word (end_mark_bit);
    unsigned int result = 0;

    unsigned int firstwrd = ~(lowbits (~0, startbit));
    unsigned int lastwrd = ~(highbits (~0, endbit));

    if (startwrd == endwrd)
    {
        unsigned int wrd = firstwrd & lastwrd;
        result = mark_array[startwrd] & wrd;
        if (result)
        {
            FATAL_GC_ERROR();
        }
        return;
    }

    // verify the first mark word is cleared.
    if (startbit)
    {
        result = mark_array[startwrd] & firstwrd;
        if (result)
        {
            FATAL_GC_ERROR();
        }
        startwrd++;
    }

    for (size_t wrdtmp = startwrd; wrdtmp < endwrd; wrdtmp++)
    {
        result = mark_array[wrdtmp];
        if (result)
        {
            FATAL_GC_ERROR();
        }
    }

    // set the last mark word.
    if (endbit)
    {
        result = mark_array[endwrd] & lastwrd;
        if (result)
        {
            FATAL_GC_ERROR();
        }
    }
#endif //VERIFY_HEAP
}

void gc_heap::verify_mark_array_cleared()
{
#ifdef VERIFY_HEAP
    if (gc_heap::background_running_p() &&
        (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_GC))
    {
        for (int i = get_start_generation_index(); i < total_generation_count; i++)
        {
            generation* gen = generation_of (i);
            heap_segment* seg = heap_segment_rw (generation_start_segment (gen));

            while (seg)
            {
                bgc_verify_mark_array_cleared (seg);
                seg = heap_segment_next_rw (seg);
            }
        }
    }
#endif //VERIFY_HEAP
}

#endif //BACKGROUND_GC

// This function is called to make sure we don't mess up the segment list
// in SOH. It's called by:
// 1) begin and end of ephemeral GCs
// 2) during bgc sweep when we switch segments.
void gc_heap::verify_soh_segment_list()
{
#ifdef VERIFY_HEAP
    if (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_GC)
    {
        for (int i = get_start_generation_index(); i <= max_generation; i++)
        {
            generation* gen = generation_of (i);
            heap_segment* seg = heap_segment_rw (generation_start_segment (gen));
            heap_segment* last_seg = 0;
            while (seg)
            {
                last_seg = seg;
                seg = heap_segment_next_rw (seg);
            }
#ifdef USE_REGIONS
            if (last_seg != generation_tail_region (gen))
#else
            if (last_seg != ephemeral_heap_segment)
#endif //USE_REGIONS
            {
                FATAL_GC_ERROR();
            }
        }
    }
#endif //VERIFY_HEAP
}

// This function can be called at any foreground GCs or blocking GCs. For background GCs,
// it can be called at the end of the final marking; and at any point during background
// sweep.
// NOTE - to be able to call this function during background sweep, we need to temporarily
// NOT clear the mark array bits as we go.
#ifdef BACKGROUND_GC
void gc_heap::verify_partial()
{
    // Different ways to fail.
    BOOL mark_missed_p = FALSE;
    BOOL bad_ref_p = FALSE;
    BOOL free_ref_p = FALSE;

    for (int i = get_start_generation_index(); i < total_generation_count; i++)
    {
        generation* gen = generation_of (i);
        int align_const = get_alignment_constant (i == max_generation);
        heap_segment* seg = heap_segment_rw (generation_start_segment (gen));

        while (seg)
        {
            uint8_t* o = heap_segment_mem (seg);
            uint8_t* end = heap_segment_allocated (seg);

            while (o < end)
            {
                size_t s = size (o);

                BOOL marked_p = background_object_marked (o, FALSE);

                if (marked_p)
                {
                    go_through_object_cl (method_table (o), o, s, oo,
                        {
                            if (*oo)
                            {
                                //dprintf (3, ("VOM: verifying member %zx in obj %zx", (size_t)*oo, o));
                                MethodTable *pMT = method_table (*oo);

                                if (pMT == g_gc_pFreeObjectMethodTable)
                                {
                                    free_ref_p = TRUE;
                                    FATAL_GC_ERROR();
                                }

                                if (!pMT->SanityCheck())
                                {
                                    bad_ref_p = TRUE;
                                    dprintf (1, ("Bad member of %zx %zx",
                                                (size_t)oo, (size_t)*oo));
                                    FATAL_GC_ERROR();
                                }

                                if (current_bgc_state == bgc_final_marking)
                                {
                                    if (marked_p && !background_object_marked (*oo, FALSE))
                                    {
                                        mark_missed_p = TRUE;
                                        FATAL_GC_ERROR();
                                    }
                                }
                            }
                        }
                    );
                }

                o = o + Align(s, align_const);
            }
            seg = heap_segment_next_rw (seg);
        }
    }
}

#endif //BACKGROUND_GC
#ifdef VERIFY_HEAP
void gc_heap::verify_committed_bytes_per_heap()
{
    size_t committed_bookkeeping = 0; // unused
    for (int oh = soh; oh < total_oh_count; oh++)
    {
#ifdef MULTIPLE_HEAPS
        assert (committed_by_oh_per_heap[oh] == compute_committed_bytes_per_heap (oh, committed_bookkeeping));
#else
        assert (committed_by_oh[oh] == compute_committed_bytes_per_heap (oh, committed_bookkeeping));
#endif //MULTIPLE_HEAPS
    }
}

void gc_heap::verify_committed_bytes()
{
    size_t total_committed = 0;
    size_t committed_decommit; // unused
    size_t committed_free; // unused
    size_t committed_bookkeeping = 0;
    size_t new_current_total_committed;
    size_t new_current_total_committed_bookkeeping;
    size_t new_committed_by_oh[recorded_committed_bucket_counts];
    compute_committed_bytes(total_committed, committed_decommit, committed_free,
                            committed_bookkeeping, new_current_total_committed, new_current_total_committed_bookkeeping,
                            new_committed_by_oh);
#ifdef MULTIPLE_HEAPS
    for (int h = 0; h < n_heaps; h++)
    {
        for (int oh = soh; oh < total_oh_count; oh++)
        {
            assert (g_heaps[h]->committed_by_oh_per_heap[oh] == g_heaps[h]->committed_by_oh_per_heap_refresh[oh]);
        }
    }
    for (int i = 0; i < recorded_committed_bucket_counts; i++)
    {
        assert (new_committed_by_oh[i] == committed_by_oh[i]);
    }
#endif //MULTIPLE_HEAPS
    assert (new_current_total_committed_bookkeeping == current_total_committed_bookkeeping);
    assert (new_current_total_committed == current_total_committed);
}

#ifdef USE_REGIONS
void gc_heap::verify_regions (int gen_number, bool can_verify_gen_num, bool can_verify_tail)
{
#ifdef _DEBUG
    // For the given generation, verify that
    //
    // 1) it has at least one region.
    // 2) the tail region is the same as the last region if we following the list of regions
    // in that generation.
    // 3) no region is pointing to itself.
    // 4) if we can verify gen num, each region's gen_num and plan_gen_num are the same and
    // they are the right generation.
    generation* gen = generation_of (gen_number);
    int num_regions_in_gen = 0;
    heap_segment* seg_in_gen = heap_segment_rw (generation_start_segment (gen));
    heap_segment* prev_region_in_gen = 0;
    heap_segment* tail_region = generation_tail_region (gen);

    while (seg_in_gen)
    {
        if (can_verify_gen_num)
        {
            if (heap_segment_gen_num (seg_in_gen) != min (gen_number, (int)max_generation))
            {
                dprintf (REGIONS_LOG, ("h%d gen%d region %p(%p) gen is %d!",
                    heap_number, gen_number, seg_in_gen, heap_segment_mem (seg_in_gen),
                    heap_segment_gen_num (seg_in_gen)));
                FATAL_GC_ERROR();
            }
            if (heap_segment_gen_num (seg_in_gen) != heap_segment_plan_gen_num (seg_in_gen))
            {
                dprintf (REGIONS_LOG, ("h%d gen%d region %p(%p) gen is %d but plan gen is %d!!",
                    heap_number, gen_number, seg_in_gen, heap_segment_mem (seg_in_gen),
                    heap_segment_gen_num (seg_in_gen), heap_segment_plan_gen_num (seg_in_gen)));
                FATAL_GC_ERROR();
            }
        }

        if (heap_segment_allocated (seg_in_gen) > heap_segment_reserved (seg_in_gen))
        {
            dprintf (REGIONS_LOG, ("h%d gen%d region %p alloc %p > reserved %p!!",
                heap_number, gen_number, heap_segment_mem (seg_in_gen),
                heap_segment_allocated (seg_in_gen), heap_segment_reserved (seg_in_gen)));
            FATAL_GC_ERROR();
        }

        prev_region_in_gen = seg_in_gen;
        num_regions_in_gen++;
        heap_segment* next_region = heap_segment_next (seg_in_gen);
        if (seg_in_gen == next_region)
        {
            dprintf (REGIONS_LOG, ("h%d gen%d region %p(%p) pointing to itself!!",
                heap_number, gen_number, seg_in_gen, heap_segment_mem (seg_in_gen)));
            FATAL_GC_ERROR();
        }
        seg_in_gen = next_region;
    }

    if (num_regions_in_gen == 0)
    {
        dprintf (REGIONS_LOG, ("h%d gen%d has no regions!!", heap_number, gen_number));
        FATAL_GC_ERROR();
    }

    if (can_verify_tail && (tail_region != prev_region_in_gen))
    {
        dprintf (REGIONS_LOG, ("h%d gen%d tail region is %p(%p), diff from last region %p(%p)!!",
            heap_number, gen_number,
            tail_region, heap_segment_mem (tail_region),
            prev_region_in_gen, heap_segment_mem (prev_region_in_gen)));
        FATAL_GC_ERROR();
    }
#endif // _DEBUG
}

inline bool is_user_alloc_gen (int gen_number)
{
    return ((gen_number == soh_gen0) || (gen_number == loh_generation) || (gen_number == poh_generation));
}

void gc_heap::verify_regions (bool can_verify_gen_num, bool concurrent_p)
{
#ifdef _DEBUG
    for (int i = 0; i < total_generation_count; i++)
    {
        bool can_verify_tail = (concurrent_p ? !is_user_alloc_gen (i) : true);
        verify_regions (i, can_verify_gen_num, can_verify_tail);

        if (can_verify_gen_num &&
            can_verify_tail &&
            (i >= max_generation))
        {
            verify_committed_bytes_per_heap ();
        }
    }
#endif // _DEBUG
}

#endif //USE_REGIONS

BOOL gc_heap::check_need_card (uint8_t* child_obj, int gen_num_for_cards,
                               uint8_t* low, uint8_t* high)
{
#ifdef USE_REGIONS
    return (is_in_heap_range (child_obj) && (get_region_gen_num (child_obj) < gen_num_for_cards));
#else
    return ((child_obj < high) && (child_obj >= low));
#endif //USE_REGIONS
}

void gc_heap::enter_gc_lock_for_verify_heap()
{
#ifdef VERIFY_HEAP
    if (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_GC)
    {
        enter_spin_lock (&gc_heap::gc_lock);
        dprintf (SPINLOCK_LOG, ("enter gc_lock for verify_heap"));
    }
#endif // VERIFY_HEAP
}

void gc_heap::leave_gc_lock_for_verify_heap()
{
#ifdef VERIFY_HEAP
    if (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_GC)
    {
        dprintf (SPINLOCK_LOG, ("leave gc_lock taken for verify_heap"));
        leave_spin_lock (&gc_heap::gc_lock);
    }
#endif // VERIFY_HEAP
}

void gc_heap::verify_heap (BOOL begin_gc_p)
{
    int heap_verify_level = static_cast<int>(GCConfig::GetHeapVerifyLevel());

#ifdef MULTIPLE_HEAPS
    t_join* current_join = &gc_t_join;
#ifdef BACKGROUND_GC
    if (settings.concurrent && (bgc_thread_id.IsCurrentThread()))
    {
        // We always call verify_heap on entry of GC on the SVR GC threads.
        current_join = &bgc_t_join;
    }
#endif //BACKGROUND_GC
#endif //MULTIPLE_HEAPS

#ifndef TRACE_GC
    UNREFERENCED_PARAMETER(begin_gc_p);
#endif //!TRACE_GC

#ifdef BACKGROUND_GC
    dprintf (2,("[%s]GC#%zu(%s): Verifying heap - begin",
        (begin_gc_p ? "BEG" : "END"),
        VolatileLoad(&settings.gc_index),
        get_str_gc_type()));
#else
    dprintf (2,("[%s]GC#%zu: Verifying heap - begin",
                (begin_gc_p ? "BEG" : "END"), VolatileLoad(&settings.gc_index)));
#endif //BACKGROUND_GC

#ifndef MULTIPLE_HEAPS
#ifndef USE_REGIONS
    if ((ephemeral_low != generation_allocation_start (generation_of (max_generation - 1))) ||
        (ephemeral_high != heap_segment_reserved (ephemeral_heap_segment)))
    {
        FATAL_GC_ERROR();
    }
#endif //!USE_REGIONS
#endif //MULTIPLE_HEAPS

#ifdef BACKGROUND_GC
    //don't touch the memory because the program is allocating from it.
    if (!settings.concurrent)
#endif //BACKGROUND_GC
    {
        if (!(heap_verify_level & GCConfig::HEAPVERIFY_NO_MEM_FILL))
        {
            // 0xaa the unused portions of segments.
            for (int i = get_start_generation_index(); i < total_generation_count; i++)
            {
                generation* gen1 = generation_of (i);
                heap_segment* seg1 = heap_segment_rw (generation_start_segment (gen1));

                while (seg1)
                {
                    uint8_t* clear_start = heap_segment_allocated (seg1) - plug_skew;
                    if (heap_segment_used (seg1) > clear_start)
                    {
                        dprintf (3, ("setting end of seg %p: [%p-[%p to 0xaa",
                            heap_segment_mem (seg1),
                            clear_start ,
                            heap_segment_used (seg1)));
                        memset (heap_segment_allocated (seg1) - plug_skew, 0xaa,
                            (heap_segment_used (seg1) - clear_start));
                    }
                    seg1 = heap_segment_next_rw (seg1);
                }
            }
        }
    }

#ifndef USE_REGIONS
#ifdef MULTIPLE_HEAPS
    current_join->join(this, gc_join_verify_copy_table);
    if (current_join->joined())
#endif //MULTIPLE_HEAPS
    {
        // in concurrent GC, new segment could be allocated when GC is working so the card brick table might not be updated at this point
        copy_brick_card_table_on_growth ();

#ifdef MULTIPLE_HEAPS
        current_join->restart();
#endif //MULTIPLE_HEAPS
    }
#endif //!USE_REGIONS

    //verify that the generation structures makes sense
    {
#ifdef _DEBUG
#ifdef USE_REGIONS
        verify_regions (true, settings.concurrent);
#else //USE_REGIONS
        generation* gen = generation_of (max_generation);

        assert (generation_allocation_start (gen) ==
                heap_segment_mem (heap_segment_rw (generation_start_segment (gen))));
        int gen_num = max_generation-1;
        generation* prev_gen = gen;
        while (gen_num >= 0)
        {
            gen = generation_of (gen_num);
            assert (generation_allocation_segment (gen) == ephemeral_heap_segment);
            assert (generation_allocation_start (gen) >= heap_segment_mem (ephemeral_heap_segment));
            assert (generation_allocation_start (gen) < heap_segment_allocated (ephemeral_heap_segment));

            if (generation_start_segment (prev_gen ) ==
                generation_start_segment (gen))
            {
                assert (generation_allocation_start (prev_gen) <
                        generation_allocation_start (gen));
            }
            prev_gen = gen;
            gen_num--;
        }
#endif //USE_REGIONS
#endif //_DEBUG
    }

    size_t          total_objects_verified = 0;
    size_t          total_objects_verified_deep = 0;

    BOOL            bCurrentBrickInvalid = FALSE;
    size_t          last_valid_brick = 0;
    size_t          curr_brick = 0;
    size_t          prev_brick = (size_t)-1;
    int             gen_num_for_cards = 0;
#ifdef USE_REGIONS
    int             gen_num_to_stop = 0;
    uint8_t*        e_high = 0;
    uint8_t*        next_boundary = 0;
#else //USE_REGIONS
    // For no regions the gen number is separately reduced when we detect the ephemeral seg.
    int gen_num_to_stop = max_generation;
    uint8_t*        e_high = ephemeral_high;
    uint8_t*        next_boundary = generation_allocation_start (generation_of (max_generation - 1));
    uint8_t*        begin_youngest = generation_allocation_start(generation_of(0));
#endif //!USE_REGIONS

    // go through all generations starting with the highest
    for (int curr_gen_num = total_generation_count - 1; curr_gen_num >= gen_num_to_stop; curr_gen_num--)
    {
        int             align_const = get_alignment_constant (curr_gen_num == max_generation);
        BOOL            large_brick_p = (curr_gen_num > max_generation);
#ifdef USE_REGIONS
        gen_num_for_cards = ((curr_gen_num >= max_generation) ? max_generation : curr_gen_num);
#endif //USE_REGIONS
        heap_segment*   seg = heap_segment_in_range (generation_start_segment (generation_of (curr_gen_num) ));

        while (seg)
        {
            uint8_t*        curr_object = heap_segment_mem (seg);
            uint8_t*        prev_object = 0;

            bool verify_bricks_p = true;
#ifdef USE_REGIONS
            if (heap_segment_read_only_p(seg))
            {
                dprintf(1, ("seg %zx is ro! Shouldn't happen with regions", (size_t)seg));
                FATAL_GC_ERROR();
            }
            if (heap_segment_gen_num (seg) != heap_segment_plan_gen_num (seg))
            {
                dprintf (1, ("Seg %p, gen num is %d, plan gen num is %d",
                    heap_segment_mem (seg), heap_segment_gen_num (seg), heap_segment_plan_gen_num (seg)));
                FATAL_GC_ERROR();
            }
#else //USE_REGIONS
            if (heap_segment_read_only_p(seg))
            {
                size_t current_brick = brick_of(max(heap_segment_mem(seg), lowest_address));
                size_t end_brick = brick_of(min(heap_segment_reserved(seg), highest_address) - 1);
                while (current_brick <= end_brick)
                {
                    if (brick_table[current_brick] != 0)
                    {
                        dprintf(1, ("Verifying Heap: %zx brick of a frozen segment is not zeroed", current_brick));
                        FATAL_GC_ERROR();
                    }
                    current_brick++;
                }
                verify_bricks_p = false;
            }
#endif //USE_REGIONS

#ifdef BACKGROUND_GC
            BOOL consider_bgc_mark_p    = FALSE;
            BOOL check_current_sweep_p  = FALSE;
            BOOL check_saved_sweep_p    = FALSE;
            should_check_bgc_mark (seg, &consider_bgc_mark_p, &check_current_sweep_p, &check_saved_sweep_p);
#endif //BACKGROUND_GC

            while (curr_object < heap_segment_allocated (seg))
            {
                if (is_mark_set (curr_object))
                {
                    dprintf (1, ("curr_object: %zx is marked!",(size_t)curr_object));
                    FATAL_GC_ERROR();
                }

                size_t s = size (curr_object);
                dprintf (3, ("o: %zx, s: %zu", (size_t)curr_object, s));
                if (s == 0)
                {
                    dprintf (1, ("Verifying Heap: size of current object %p == 0", curr_object));
                    FATAL_GC_ERROR();
                }

#ifndef USE_REGIONS
                // handle generation boundaries within ephemeral segment
                if (seg == ephemeral_heap_segment)
                {
                    if ((curr_gen_num > 0) && (curr_object >= next_boundary))
                    {
                        curr_gen_num--;
                        if (curr_gen_num > 0)
                        {
                            next_boundary = generation_allocation_start (generation_of (curr_gen_num - 1));
                        }
                    }
                }
#endif //!USE_REGIONS

#ifdef USE_REGIONS
                if (verify_bricks_p && curr_gen_num != 0)
#else
                // If object is not in the youngest generation, then lets
                // verify that the brick table is correct....
                if (verify_bricks_p && ((seg != ephemeral_heap_segment) ||
                     (brick_of(curr_object) < brick_of(begin_youngest))))
#endif //USE_REGIONS
                {
                    curr_brick = brick_of(curr_object);

                    // Brick Table Verification...
                    //
                    // On brick transition
                    //     if brick is negative
                    //          verify that brick indirects to previous valid brick
                    //     else
                    //          set current brick invalid flag to be flipped if we
                    //          encounter an object at the correct place
                    //
                    if (curr_brick != prev_brick)
                    {
                        // If the last brick we were examining had positive
                        // entry but we never found the matching object, then
                        // we have a problem
                        // If prev_brick was the last one of the segment
                        // it's ok for it to be invalid because it is never looked at
                        if (bCurrentBrickInvalid &&
                            (curr_brick != brick_of (heap_segment_mem (seg))) &&
                            !heap_segment_read_only_p (seg))
                        {
                            dprintf (1, ("curr brick %zx invalid", curr_brick));
                            FATAL_GC_ERROR();
                        }

                        if (large_brick_p)
                        {
                            //large objects verify the table only if they are in
                            //range.
                            if ((heap_segment_reserved (seg) <= highest_address) &&
                                (heap_segment_mem (seg) >= lowest_address) &&
                                brick_table [curr_brick] != 0)
                            {
                                dprintf (1, ("curr_brick %zx for large object %zx is set to %zx",
                                    curr_brick, (size_t)curr_object, (size_t)brick_table[curr_brick]));
                                FATAL_GC_ERROR();
                            }
                            else
                            {
                                bCurrentBrickInvalid = FALSE;
                            }
                        }
                        else
                        {
                            // If the current brick contains a negative value make sure
                            // that the indirection terminates at the last  valid brick
                            if (brick_table [curr_brick] <= 0)
                            {
                                if (brick_table [curr_brick] == 0)
                                {
                                    dprintf(1, ("curr_brick %zx for object %zx set to 0",
                                            curr_brick, (size_t)curr_object));
                                    FATAL_GC_ERROR();
                                }
                                ptrdiff_t i = curr_brick;
                                while ((i >= ((ptrdiff_t) brick_of (heap_segment_mem (seg)))) &&
                                       (brick_table[i] < 0))
                                {
                                    i = i + brick_table[i];
                                }
                                if (i <  ((ptrdiff_t)(brick_of (heap_segment_mem (seg))) - 1))
                                {
                                    dprintf (1, ("ptrdiff i: %zx < brick_of (heap_segment_mem (seg)):%zx - 1. curr_brick: %zx",
                                            i, brick_of (heap_segment_mem (seg)),
                                            curr_brick));
                                    FATAL_GC_ERROR();
                                }
                                bCurrentBrickInvalid = FALSE;
                            }
                            else if (!heap_segment_read_only_p (seg))
                            {
                                bCurrentBrickInvalid = TRUE;
                            }
                        }
                    }

                    if (bCurrentBrickInvalid)
                    {
                        if (curr_object == (brick_address(curr_brick) + brick_table[curr_brick] - 1))
                        {
                            bCurrentBrickInvalid = FALSE;
                            last_valid_brick = curr_brick;
                        }
                    }
                }

                if (*((uint8_t**)curr_object) != (uint8_t *) g_gc_pFreeObjectMethodTable)
                {
#ifdef FEATURE_LOH_COMPACTION
                    if ((curr_gen_num == loh_generation) && (prev_object != 0))
                    {
                        assert (method_table (prev_object) == g_gc_pFreeObjectMethodTable);
                    }
#endif //FEATURE_LOH_COMPACTION

                    total_objects_verified++;

                    BOOL can_verify_deep = TRUE;
#ifdef BACKGROUND_GC
                    can_verify_deep = fgc_should_consider_object (curr_object, seg, consider_bgc_mark_p, check_current_sweep_p, check_saved_sweep_p);
#endif //BACKGROUND_GC

                    BOOL deep_verify_obj = can_verify_deep;
                    if ((heap_verify_level & GCConfig::HEAPVERIFY_DEEP_ON_COMPACT) && !settings.compaction)
                        deep_verify_obj = FALSE;

                    ((CObjectHeader*)curr_object)->ValidateHeap(deep_verify_obj);

                    if (can_verify_deep)
                    {
                        if (curr_gen_num > 0)
                        {
                            BOOL need_card_p = FALSE;
                            if (contain_pointers_or_collectible (curr_object))
                            {
                                dprintf (4, ("curr_object: %zx", (size_t)curr_object));
                                size_t crd = card_of (curr_object);
                                BOOL found_card_p = card_set_p (crd);

#ifdef COLLECTIBLE_CLASS
                                if (is_collectible(curr_object))
                                {
                                    uint8_t* class_obj = get_class_object (curr_object);
                                    if (check_need_card (class_obj, gen_num_for_cards, next_boundary, e_high))
                                    {
                                        if (!found_card_p)
                                        {
                                            dprintf (1, ("Card not set, curr_object = [%zx:%zx pointing to class object %p",
                                                        card_of (curr_object), (size_t)curr_object, class_obj));
                                            FATAL_GC_ERROR();
                                        }
                                    }
                                }
#endif //COLLECTIBLE_CLASS

                                if (contain_pointers(curr_object))
                                {
                                    go_through_object_nostart
                                        (method_table(curr_object), curr_object, s, oo,
                                        {
                                            if (crd != card_of ((uint8_t*)oo))
                                            {
                                                crd = card_of ((uint8_t*)oo);
                                                found_card_p = card_set_p (crd);
                                                need_card_p = FALSE;
                                            }
                                            if (*oo && check_need_card (*oo, gen_num_for_cards, next_boundary, e_high))
                                            {
                                                need_card_p = TRUE;
                                            }

                                            if (need_card_p && !found_card_p)
                                            {
                                                dprintf (1, ("(in loop) Card not set, curr_object = [%zx:%zx, %zx:%zx[",
                                                            card_of (curr_object), (size_t)curr_object,
                                                            card_of (curr_object+Align(s, align_const)),
                                                            (size_t)(curr_object+Align(s, align_const))));
                                                FATAL_GC_ERROR();
                                            }
                                        }
                                            );
                                }
                                if (need_card_p && !found_card_p)
                                {
                                    dprintf (1, ("Card not set, curr_object = [%zx:%zx, %zx:%zx[",
                                        card_of (curr_object), (size_t)curr_object,
                                        card_of (curr_object + Align(s, align_const)),
                                        (size_t)(curr_object + Align(s, align_const))));
                                    FATAL_GC_ERROR();
                                }
                            }
                        }
                        total_objects_verified_deep++;
                    }
                }

                prev_object = curr_object;
                prev_brick = curr_brick;
                curr_object = curr_object + Align(s, align_const);
                if (curr_object < prev_object)
                {
                    dprintf (1, ("overflow because of a bad object size: %p size %zx", prev_object, s));
                    FATAL_GC_ERROR();
                }
            }

            if (curr_object > heap_segment_allocated(seg))
            {
                dprintf (1, ("Verifiying Heap: curr_object: %zx > heap_segment_allocated (seg: %zx) %p",
                        (size_t)curr_object, (size_t)seg, heap_segment_allocated (seg)));
                FATAL_GC_ERROR();
            }

            seg = heap_segment_next_in_range (seg);
        }
    }

#ifdef BACKGROUND_GC
    dprintf (2, ("(%s)(%s)(%s) total_objects_verified is %zd, total_objects_verified_deep is %zd",
                 get_str_gc_type(),
                 (begin_gc_p ? "BEG" : "END"),
                 ((current_c_gc_state == c_gc_state_planning) ? "in plan" : "not in plan"),
                 total_objects_verified, total_objects_verified_deep));
    if (current_c_gc_state != c_gc_state_planning)
    {
        assert (total_objects_verified == total_objects_verified_deep);
    }
#endif //BACKGROUND_GC

    verify_free_lists();

#ifdef FEATURE_PREMORTEM_FINALIZATION
    finalize_queue->CheckFinalizerObjects();
#endif // FEATURE_PREMORTEM_FINALIZATION

    {
        // to be consistent with handle table APIs pass a ScanContext*
        // to provide the heap number.  the SC isn't complete though so
        // limit its scope to handle table verification.
        ScanContext sc;
        sc.thread_number = heap_number;
        sc.thread_count = n_heaps;
        GCScan::VerifyHandleTable(max_generation, max_generation, &sc);
    }

#ifdef MULTIPLE_HEAPS
    current_join->join(this, gc_join_verify_objects_done);
    if (current_join->joined())
#endif //MULTIPLE_HEAPS
    {
        GCToEEInterface::VerifySyncTableEntry();
#ifdef MULTIPLE_HEAPS
#ifdef USE_REGIONS
        // check that the heaps not in use have not been inadvertently written to
        for (int hn = n_heaps; hn < n_max_heaps; hn++)
        {
            gc_heap* hp = g_heaps[hn];
            hp->check_decommissioned_heap();
        }
#endif //USE_REGIONS

        current_join->restart();
#endif //MULTIPLE_HEAPS
    }

#ifdef FEATURE_EVENT_TRACE
    string_dedup_queue_verify();
#endif //FEATURE_EVENT_TRACE

#ifdef BACKGROUND_GC
    if (settings.concurrent)
    {
        verify_mark_array_cleared();
    }
    dprintf (2,("GC%zu(%s): Verifying heap - end",
        VolatileLoad(&settings.gc_index),
        get_str_gc_type()));
#else
    dprintf (2,("GC#d: Verifying heap - end", VolatileLoad(&settings.gc_index)));
#endif //BACKGROUND_GC
}

#endif //VERIFY_HEAP
#ifdef BACKGROUND_GC
void gc_heap::add_bgc_pause_duration_0()
{
    if (settings.concurrent)
    {
        uint64_t suspended_end_ts = GetHighPrecisionTimeStamp();
        size_t pause_duration = (size_t)(suspended_end_ts - suspended_start_time);
        last_recorded_gc_info* last_gc_info = &(last_bgc_info[last_bgc_info_index]);
        last_gc_info->pause_durations[0] = pause_duration;
        if (last_gc_info->index < last_ephemeral_gc_info.index)
        {
            last_gc_info->pause_durations[0] -= last_ephemeral_gc_info.pause_durations[0];
        }

        total_suspended_time += last_gc_info->pause_durations[0];
    }
}

last_recorded_gc_info* gc_heap::get_completed_bgc_info()
{
    int completed_bgc_index = gc_heap::background_running_p() ?
        (int)(!(gc_heap::last_bgc_info_index)) : (int)gc_heap::last_bgc_info_index;
    return &gc_heap::last_bgc_info[completed_bgc_index];
}

#endif //BACKGROUND_GC

const char* gc_heap::get_str_gc_type()
{
#ifdef BACKGROUND_GC
    return (settings.concurrent ? "BGC" : (gc_heap::background_running_p () ? "FGC" : "NGC"));
#else // BACKGROUND_GC
    return "NGC";
#endif // BACKGROUND_GC
}

#ifdef GC_CONFIG_DRIVEN
void gc_heap::record_interesting_info_per_heap()
{
    // datapoints are always from the last blocking GC so don't record again
    // for BGCs.
    if (!(settings.concurrent))
    {
        for (int i = 0; i < max_idp_count; i++)
        {
            interesting_data_per_heap[i] += interesting_data_per_gc[i];
        }
    }

    int compact_reason = get_gc_data_per_heap()->get_mechanism (gc_heap_compact);
    if (compact_reason >= 0)
        (compact_reasons_per_heap[compact_reason])++;
    int expand_mechanism = get_gc_data_per_heap()->get_mechanism (gc_heap_expand);
    if (expand_mechanism >= 0)
        (expand_mechanisms_per_heap[expand_mechanism])++;

    for (int i = 0; i < max_gc_mechanism_bits_count; i++)
    {
        if (get_gc_data_per_heap()->is_mechanism_bit_set ((gc_mechanism_bit_per_heap)i))
            (interesting_mechanism_bits_per_heap[i])++;
    }

    //         h#  | GC   | gen | C   | EX  | NF  | BF  | ML  | DM  || PreS | PostS | Merge | Conv | Pre | Post | PrPo | PreP | PostP |
    cprintf (("%2d | %6zu | %1d | %1s | %2s | %2s | %2s | %2s | %2s || %5zu | %5zu | %5zu | %5zu | %5zu | %5zu | %5zu | %5zu | %5zu |",
            heap_number,
            (size_t)settings.gc_index,
            settings.condemned_generation,
            // TEMP - I am just doing this for wks GC 'cause I wanna see the pattern of doing C/S GCs.
            (settings.compaction ? (((compact_reason >= 0) && gc_heap_compact_reason_mandatory_p[compact_reason]) ? "M" : "W") : ""), // compaction
            ((expand_mechanism >= 0)? "X" : ""), // EX
            ((expand_mechanism == expand_reuse_normal) ? "X" : ""), // NF
            ((expand_mechanism == expand_reuse_bestfit) ? "X" : ""), // BF
            (get_gc_data_per_heap()->is_mechanism_bit_set (gc_mark_list_bit) ? "X" : ""), // ML
            (get_gc_data_per_heap()->is_mechanism_bit_set (gc_demotion_bit) ? "X" : ""), // DM
            interesting_data_per_gc[idp_pre_short],
            interesting_data_per_gc[idp_post_short],
            interesting_data_per_gc[idp_merged_pin],
            interesting_data_per_gc[idp_converted_pin],
            interesting_data_per_gc[idp_pre_pin],
            interesting_data_per_gc[idp_post_pin],
            interesting_data_per_gc[idp_pre_and_post_pin],
            interesting_data_per_gc[idp_pre_short_padded],
            interesting_data_per_gc[idp_post_short_padded]));
}

void gc_heap::record_global_mechanisms()
{
    for (int i = 0; i < max_global_mechanisms_count; i++)
    {
        if (gc_data_global.get_mechanism_p ((gc_global_mechanism_p)i))
        {
            ::record_global_mechanism (i);
        }
    }
}

#endif //GC_CONFIG_DRIVEN

//------------------------------------------------------------------------------
//
//                      End of VM specific support
//
//------------------------------------------------------------------------------
void gc_heap::walk_heap_per_heap (walk_fn fn, void* context, int gen_number, BOOL walk_large_object_heap_p)
{
    generation* gen = gc_heap::generation_of (gen_number);
    heap_segment*    seg = generation_start_segment (gen);
    uint8_t* x = ((gen_number == max_generation) ? heap_segment_mem (seg) : get_soh_start_object (seg, gen));
    uint8_t*       end = heap_segment_allocated (seg);
    int align_const = get_alignment_constant (TRUE);
    BOOL walk_pinned_object_heap = walk_large_object_heap_p;

    while (1)
    {
        if (x >= end)
        {
            if ((seg = heap_segment_next (seg)) != 0)
            {
                x = heap_segment_mem (seg);
                end = heap_segment_allocated (seg);
                continue;
            }
#ifdef USE_REGIONS
            else if (gen_number > 0)
            {
                // advance to next lower generation
                gen_number--;
                gen = gc_heap::generation_of (gen_number);
                seg = generation_start_segment (gen);

                x = heap_segment_mem (seg);
                end = heap_segment_allocated (seg);
                continue;
            }
#endif // USE_REGIONS
            else
            {
                if (walk_large_object_heap_p)
                {
                    walk_large_object_heap_p = FALSE;
                    seg = generation_start_segment (large_object_generation);
                }
                else if (walk_pinned_object_heap)
                {
                    walk_pinned_object_heap = FALSE;
                    seg = generation_start_segment (pinned_object_generation);
                }
                else
                {
                    break;
                }

                align_const = get_alignment_constant (FALSE);
                x = heap_segment_mem (seg);
                end = heap_segment_allocated (seg);
                continue;
            }
        }

        size_t s = size (x);
        CObjectHeader* o = (CObjectHeader*)x;

        if (!o->IsFree())

        {
            _ASSERTE(((size_t)o & 0x3) == 0); // Last two bits should never be set at this point

            if (!fn (o->GetObjectBase(), context))
                return;
        }
        x = x + Align (s, align_const);
    }
}

void gc_heap::walk_heap (walk_fn fn, void* context, int gen_number, BOOL walk_large_object_heap_p)
{
#ifdef MULTIPLE_HEAPS
    for (int hn = 0; hn < gc_heap::n_heaps; hn++)
    {
        gc_heap* hp = gc_heap::g_heaps [hn];

        hp->walk_heap_per_heap (fn, context, gen_number, walk_large_object_heap_p);
    }
#else
    walk_heap_per_heap(fn, context, gen_number, walk_large_object_heap_p);
#endif //MULTIPLE_HEAPS
}

#ifdef FEATURE_EVENT_TRACE

// The GC does not know the string method table, so strings are recognized by their shape: a variable sized object with two byte
// components and the base size of System.String (object header, method table pointer, length and the terminating null character).
// No array has that base size. The characters start right after the length.
const uint32_t string_dedup_base_size = (uint32_t)(sizeof (ObjHeader) + sizeof (Object) + sizeof (uint32_t) + sizeof (uint16_t));
const size_t string_dedup_chars_offset = sizeof (Object) + sizeof (uint32_t);

static bool string_dedup_is_string (MethodTable* mt)
{
    return mt->HasComponentSize() && (mt->RawGetComponentSize() == sizeof (uint16_t)) && (mt->GetBaseSize() == string_dedup_base_size);
}

// Anything other than the bits the GC uses for itself means the identity of the string is observable (hash code, lock, sync block).
static bool string_dedup_header_has_state (CObjectHeader* o)
{
    return (o->GetHeader()->GetBits() & ~(uint32_t)(BIT_SBLK_GC_RESERVE | BIT_SBLK_FINALIZER_RUN)) != 0;
}

#endif //FEATURE_EVENT_TRACE

#if defined(FEATURE_EVENT_TRACE) && !defined(MULTIPLE_HEAPS)

// String deduplication measurement (GCStringDedupMeasure): right after marking in a full blocking GC, every live string is hashed
// and the distinct contents are counted to find out how much memory deduplicating them would free. The heap is not modified.
//
// The result is reported with the StringDedupMeasure GC event. The payload of its version 1 is these uint64 values in order:
//   GC index, measurement time in microseconds, bytes of live objects, string count, string bytes,
//   strings skipped because they are pinned, strings skipped because the object header holds a hash code, a lock or a sync block,
//   candidate strings, distinct candidate contents, candidates that did not fit in the table,
//   bytes freed by deduplicating all candidates, the same counting only strings in gen2, LOH and POH,
//   then the bytes freed for each string length bucket (see string_dedup_length_bucket).

struct string_dedup_entry
{
    uint64_t hash;        // 0 marks an empty slot
    uint8_t* sample;      // one of the strings with this content
    uint32_t old_count;   // strings with this content in gen2, LOH and POH
    uint32_t young_count; // strings with this content in gen0 and gen1
};

struct string_dedup_stats
{
    string_dedup_entry* slots;
    size_t slot_count;    // always a power of two
    size_t used_slots;
    uint64_t live_bytes;
    uint64_t string_count;
    uint64_t string_bytes;
    uint64_t pinned_count;
    uint64_t header_state_count;
    uint64_t candidate_count;
    uint64_t untracked_count;
    bool grow_failed;     // growing the table failed, so it is not tried again for every later string
};

const size_t string_dedup_initial_slots = (size_t)1 << 16;

// With 24 bytes per slot on 64-bit the table takes up to 192 MiB, plus the 96 MiB of the previous table while it grows to that size.
const size_t string_dedup_max_slots = (size_t)1 << 23;
const int string_dedup_length_buckets = 8;

// Buckets by string length in characters: 0-3, 4-7, 8-15, 16-31, 32-63, 64-127, 128-511, 512 and longer.
static int string_dedup_length_bucket (uint32_t length)
{
    static const uint32_t bucket_limits[string_dedup_length_buckets - 1] = { 4, 8, 16, 32, 64, 128, 512 };

    int bucket = 0;
    while ((bucket < (string_dedup_length_buckets - 1)) && (length >= bucket_limits[bucket]))
    {
        bucket++;
    }

    return bucket;
}

// The quality of this hash only affects the speed of the table, equal contents are always verified with memcmp. The seed keeps the contents
// that collide from being known in advance, it is 0 where that does not matter.
static uint64_t string_dedup_hash (const uint8_t* chars, size_t byte_count, uint64_t seed)
{
    const uint64_t multiplier = 0xFF51AFD7ED558CCDULL;

    uint64_t hash = ((uint64_t)byte_count ^ seed) * 0x9E3779B97F4A7C15ULL;
    while (byte_count >= sizeof (uint64_t))
    {
        uint64_t chunk;
        memcpy (&chunk, chars, sizeof (uint64_t));
        hash = (hash ^ chunk) * multiplier;
        hash ^= hash >> 32;
        chars += sizeof (uint64_t);
        byte_count -= sizeof (uint64_t);
    }

    if (byte_count > 0)
    {
        uint64_t chunk = 0;
        memcpy (&chunk, chars, byte_count);
        hash = (hash ^ chunk) * multiplier;
        hash ^= hash >> 32;
    }

    hash *= 0xC4CEB9FE1A85EC53ULL;
    hash ^= hash >> 33;

    return (hash != 0) ? hash : 1;
}

static bool string_dedup_grow (string_dedup_stats* stats)
{
    size_t new_slot_count = stats->slot_count * 2;
    string_dedup_entry* new_slots = new (nothrow) string_dedup_entry[new_slot_count];
    if (new_slots == nullptr)
    {
        stats->grow_failed = true;
        return false;
    }

    memset (new_slots, 0, new_slot_count * sizeof (string_dedup_entry));
    for (size_t i = 0; i < stats->slot_count; i++)
    {
        if (stats->slots[i].hash != 0)
        {
            size_t index = (size_t)stats->slots[i].hash & (new_slot_count - 1);
            while (new_slots[index].hash != 0)
            {
                index = (index + 1) & (new_slot_count - 1);
            }

            new_slots[index] = stats->slots[i];
        }
    }

    delete[] stats->slots;
    stats->slots = new_slots;
    stats->slot_count = new_slot_count;

    return true;
}

// Returns the entry for the strings with the content of 'str', adding it if this content is new, or nullptr if the table is full.
static string_dedup_entry* string_dedup_find_or_add (string_dedup_stats* stats, uint64_t hash, CObjectHeader* str, uint32_t length)
{
    while (true)
    {
        size_t mask = stats->slot_count - 1;
        size_t index = (size_t)hash & mask;
        while (stats->slots[index].hash != 0)
        {
            string_dedup_entry* slot = &stats->slots[index];
            if ((slot->hash == hash) &&
                (((CObjectHeader*)slot->sample)->GetNumComponents() == length) &&
                (memcmp ((uint8_t*)slot->sample + string_dedup_chars_offset, (uint8_t*)str + string_dedup_chars_offset, (size_t)length * sizeof (uint16_t)) == 0))
            {
                return slot;
            }

            index = (index + 1) & mask;
        }

        // Linear probing: keep the table at most half full while it can still grow, at most three quarters full once it cannot.
        bool at_max_size = (stats->slot_count == string_dedup_max_slots);
        size_t max_used_slots = at_max_size ? ((stats->slot_count / 4) * 3) : (stats->slot_count / 2);
        if (stats->used_slots < max_used_slots)
        {
            string_dedup_entry* slot = &stats->slots[index];
            slot->hash = hash;
            slot->sample = (uint8_t*)str;
            slot->old_count = 0;
            slot->young_count = 0;
            stats->used_slots++;

            return slot;
        }

        if (at_max_size || stats->grow_failed || !string_dedup_grow (stats))
        {
            return nullptr;
        }
    }
}

bool gc_heap::measure_string_duplicates_visit (Object* obj, void* context)
{
    string_dedup_stats* stats = (string_dedup_stats*)context;
    CObjectHeader* o = (CObjectHeader*)obj;
    if (!o->IsMarked())
    {
        return true;
    }

#ifndef USE_REGIONS
    // Segments mark every object of an in-range frozen segment because those are never collected, regions never mark them.
    // Skip them so that both measure the same live set.
    heap_segment* seg = find_segment ((uint8_t*)o, FALSE);
    if ((seg != nullptr) && heap_segment_read_only_p (seg))
    {
        return true;
    }
#endif //!USE_REGIONS

    uint64_t object_size = Align (size ((uint8_t*)o));
    stats->live_bytes += object_size;

    MethodTable* mt = o->GetMethodTable();
    if (!string_dedup_is_string (mt))
    {
        return true;
    }

    stats->string_count++;
    stats->string_bytes += object_size;

    if (o->IsPinned())
    {
        stats->pinned_count++;
        return true;
    }

    if (string_dedup_header_has_state (o))
    {
        stats->header_state_count++;
        return true;
    }

    uint32_t length = o->GetNumComponents();
    uint64_t hash = string_dedup_hash ((uint8_t*)o + string_dedup_chars_offset, (size_t)length * sizeof (uint16_t), 0);
    stats->candidate_count++;

    string_dedup_entry* entry = string_dedup_find_or_add (stats, hash, o, length);
    if (entry == nullptr)
    {
        stats->untracked_count++;
    }
    else if (object_gennum ((uint8_t*)o) >= max_generation)
    {
        entry->old_count++;
    }
    else
    {
        entry->young_count++;
    }

    return true;
}

void gc_heap::measure_string_duplicates()
{
#if !defined(BUILD_AS_STANDALONE) && !defined(FEATURE_NATIVEAOT)
    assert (string_dedup_base_size == StringObject::GetBaseSize());
    assert (string_dedup_chars_offset == StringObject::GetBufferOffset());
#endif //!BUILD_AS_STANDALONE && !FEATURE_NATIVEAOT

    if (!GCEventEnabledStringDedupMeasure_V1())
    {
        return;
    }

    string_dedup_stats stats = {};
    stats.slot_count = string_dedup_initial_slots;
    stats.slots = new (nothrow) string_dedup_entry[stats.slot_count];
    if (stats.slots == nullptr)
    {
        return;
    }

    memset (stats.slots, 0, stats.slot_count * sizeof (string_dedup_entry));

    uint64_t start_time = GetHighPrecisionTimeStamp();
    walk_heap_per_heap (measure_string_duplicates_visit, &stats, max_generation, TRUE);

    uint64_t distinct_count = 0;
    uint64_t freed_bytes = 0;
    uint64_t freed_bytes_old = 0;
    uint64_t freed_bytes_by_length[string_dedup_length_buckets] = {};
    for (size_t i = 0; i < stats.slot_count; i++)
    {
        const string_dedup_entry* entry = &stats.slots[i];
        if (entry->hash == 0)
        {
            continue;
        }

        distinct_count++;
        uint64_t total_count = (uint64_t)entry->old_count + entry->young_count;
        if (total_count < 2)
        {
            continue;
        }

        CObjectHeader* sample = (CObjectHeader*)entry->sample;
        uint64_t string_object_size = Align (size ((uint8_t*)sample));
        uint64_t freed = (total_count - 1) * string_object_size;
        freed_bytes += freed;
        freed_bytes_by_length[string_dedup_length_bucket (sample->GetNumComponents())] += freed;
        if (entry->old_count > 1)
        {
            freed_bytes_old += ((uint64_t)entry->old_count - 1) * string_object_size;
        }
    }

    uint64_t elapsed_time = GetHighPrecisionTimeStamp() - start_time;

    GCEventFireStringDedupMeasure_V1 (
        (uint64_t)settings.gc_index,
        elapsed_time,
        stats.live_bytes,
        stats.string_count,
        stats.string_bytes,
        stats.pinned_count,
        stats.header_state_count,
        stats.candidate_count,
        distinct_count,
        stats.untracked_count,
        freed_bytes,
        freed_bytes_old,
        freed_bytes_by_length[0],
        freed_bytes_by_length[1],
        freed_bytes_by_length[2],
        freed_bytes_by_length[3],
        freed_bytes_by_length[4],
        freed_bytes_by_length[5],
        freed_bytes_by_length[6],
        freed_bytes_by_length[7]
    );

    delete[] stats.slots;
}

#endif //FEATURE_EVENT_TRACE && !MULTIPLE_HEAPS

#ifdef FEATURE_EVENT_TRACE

// Candidate queue (GCStringDedupQueue): every blocking GC that condemns gen1 queues the live strings that are about to leave gen1, the
// strings that survived two GCs and are likely to stay around. A queued string is only a candidate for deduplication: nothing is hashed,
// compared or changed here. The entries do not keep their strings alive, like short weak handles: whoever frees or moves a queued string
// has to update the queue, so the queue stays valid for whoever consumes it later.
//
// The entries only point at strings of gen2. A string that the GC leaves in a younger generation, because it is in or next to a pinned
// plug, is removed after planning. Only a gen2 GC frees or moves a string of gen2, so only those look at the old entries: a blocking gen2
// GC removes the entries of the strings that died and relocates the others, and a background GC removes the entries of the strings that
// its final mark did not reach, before its sweep frees them. A gen1 GC only relocates the entries it queued itself.
//
// Every heap has its own queue that is filled from its own gen1. The entries can point into any heap, because the regions of a heap
// that goes out of service move to the other heaps, which is fine because the mark bits and the relocation information are global.
// When a heap goes out of service its entries are merged into another heap, like its finalization queue.
//
// The result is reported with the StringDedupQueue GC event, once for every heap and blocking or background GC. The payload of its
// version 3 is these uint64 values in order:
//   GC index, status (0 active, 1 inactive because the queue could not be allocated), condemned generation,
//   live strings in gen1, strings queued, strings not queued because the queue is full,
//   strings skipped because they are pinned, strings skipped because the object header holds a hash code, a lock or a sync block,
//   entries removed because their string died, entries relocated by compaction, entries in the queue after the GC,
//   time the GC spent on the queue in microseconds, heap number,
//   entries removed because the GC left their string in a younger generation, 1 for a background GC and 0 for a blocking GC.

struct string_dedup_candidate
{
    uint8_t* obj;
    uint64_t signature; // length and first characters when queued, to check that the entry still points at the same string
};

struct string_dedup_queue_stats
{
    uint64_t gc_index;
    uint64_t status;
    uint64_t condemned_generation;
    uint64_t seen;
    uint64_t enqueued;
    uint64_t dropped;
    uint64_t skipped_pinned;
    uint64_t skipped_header;
    uint64_t died;
    uint64_t relocated;
    uint64_t elapsed_us;
    uint64_t heap_index;
    uint64_t demoted;
    uint64_t background;
    bool report_pending;
};

#ifndef MULTIPLE_HEAPS

// Finder (GCStringDedupFind, workstation GC only): after every blocking GC the queued candidates are hashed and looked up in a table of
// canonical strings, the first string that was seen with each content. A candidate that finds another string with its content is a
// duplicate. The heap is not modified, a later step is going to redirect the references to the duplicates to the canonical string.
//
// The table does not keep its strings alive either and is maintained like the queue, so all its entries point at strings of gen2: a gen2 GC
// or a background GC removes the entries of the strings that died, a compacting gen2 GC relocates the others and the entries of the strings
// that a GC left in a younger generation are removed after planning. The candidates are consumed right after marking, while no string has
// been touched by the plan phase (it can overwrite the first bytes of a string until the GC has compacted), so the queue is empty after every
// GC and a GC only hashes the strings it queued itself. The canonical strings that it adds are still in gen1 then. A GC that does not collect
// gen2 relocates them and removes the ones it leaves in a younger generation, which are the last entries of the table, and leaves the others.
//
// Every GC that collects gen2 visits all the entries, so the table is made for that: the strings are in an array in the order in which they
// were added, which is mostly the address order of the batch that a GC promoted, and a separate index finds them by their hash. A pass only
// reads the array, sequentially, and moving the strings leaves the index alone. A removed string leaves a hole in the array and a slot of
// the index that points at it, both are dropped when the table is rebuilt, which also happens when it has become much smaller than its size.
//
// A GC that does not collect gen2 cannot tell whether the canonical string of a duplicate is still alive, it is only removed from the table
// by the next gen2 or background GC. Whoever redirects references to a canonical string has to look at its mark bit in that GC first.
// HeapVerify assumes that the strings of the table are not changed in place, because it checks their hash.
//
// The result is reported with the StringDedupFind GC event, once for every blocking or background GC. The payload of its version 1 is these
// uint64 values in order:
//   GC index, status (0 active, 1 inactive because the table could not be allocated), condemned generation,
//   candidates examined, candidates skipped because the object header holds a hash code, a lock or a sync block,
//   duplicates found, bytes of the duplicates, canonical strings added, candidates not added because the table is full,
//   entries removed because their string died, entries removed because the GC left their string in a younger generation,
//   entries in the table after the GC, time the GC spent on the finder in microseconds, 1 for a background GC and 0 for a blocking GC.

struct string_dedup_slot
{
    uint32_t entry; // 1 + the position of the string in 'strings', 0 for a slot that has never been used
    uint32_t hash;  // hash of the content
};

struct string_dedup_table
{
    uint8_t** strings;        // the canonical strings in the order in which they were added, nullptr for a removed one
    uint32_t* hashes;         // the hash of every string of 'strings', to rebuild the index
    string_dedup_slot* slots; // the index, open addressing, the slot of a removed string is skipped
    size_t count;             // used elements of 'strings' and 'hashes', including the removed strings
    size_t capacity;          // allocated elements of 'strings' and 'hashes'
    size_t max_capacity;      // the table does not grow beyond it
    size_t slot_count;        // a power of two, the slots are at most half used, at most three quarters at the largest size
    size_t max_slot_count;    // a power of two
    size_t live;              // strings that have not been removed
    uint64_t seed;
};

struct string_dedup_find_stats
{
    uint64_t gc_index;
    uint64_t status;
    uint64_t condemned_generation;
    uint64_t candidates;
    uint64_t skipped_header;
    uint64_t duplicates;
    uint64_t duplicate_bytes;
    uint64_t added;
    uint64_t not_added;
    uint64_t died;
    uint64_t demoted;
    uint64_t elapsed_us;
    uint64_t background;
};

const size_t string_dedup_table_initial_capacity = (size_t)1 << 11;

// The largest size of the table is set by GCStringDedupFindTableBits: the index has at most 2^bits slots and the table holds three quarters
// of that many strings. With the default of 21 bits that is 1572864 strings in 34 MiB on 64-bit (8 bytes for the string and 4 for its hash,
// plus 8 bytes for every slot of the index), 24 bits hold twelve million strings in 272 MiB. The table it is rebuilt into comes on top.
// Every GC that collects gen2 visits all the strings, so the size is also what limits the pause that the table adds.
const int string_dedup_table_min_bits = 12;
const int string_dedup_table_max_bits = 24;

// The table starts with a capacity that the smallest allowed size has room for, and the entries of the index and the size computations have to
// fit in 32 bits.
static_assert (string_dedup_table_initial_capacity <= ((((size_t)1 << string_dedup_table_min_bits) / 4) * 3), "The table must not start larger than its smallest allowed size");
static_assert (string_dedup_table_max_bits <= 30, "The table must stay within what 32 bits can count");

#ifdef USE_REGIONS

// Merge (GCStringDedupMerge, regions and workstation GC only): a GC that compacts replaces every reference to a duplicate by a reference to
// its canonical string, so nothing refers to the duplicate anymore and the next gen2 GC reclaims it (the duplicate itself is still planned and
// compacted like any survivor). For a duplicate it is all or nothing: a reference that is left behind makes two references that were the same
// object different ones. Only the relocate phase of a compacting GC visits every reference (the roots, the survivors, the handles and the
// cards of the older generations) and rewrites them anyway, so the merge happens there: while it consumes the candidates the finder records
// the pairs of a duplicate and its canonical string, string_dedup_merge_begin registers them at the start of the relocate phase,
// relocate_address calls string_dedup_redirect for every reference it relocates before it does anything else with it (relocate_pre_plug_info is the one
// caller that passes the address of an object that a pinned plug overwrote, which is not a reference, so it switches the merge off around its call), and
// string_dedup_merge_end drops the registrations.
//
// A GC that does not compact does not merge, and neither does one that runs while a background GC is in progress, which could not tell that
// a redirect brings back a string of gen2 that it has not marked. The recorded pairs are dropped then.
//
// A gen1 GC that would sweep compacts instead when the duplicates that the finder recorded in it add up to GCStringDedupCompactMinKB or more
// (string_dedup_compaction_pays, asked by decide_on_compacting after every other reason to compact): a sweeping GC of regions promotes the survivors
// where they are and never looks at their references, and a duplicate that the GC that found it did not merge is never found again, because a
// string leaves gen1 only once. A full GC is not asked to compact: what its compaction costs depends on the whole heap, not on what its duplicates are worth.
//
// A string that a stack, a register or a byref refers to is left out altogether (the finder does not even look it up): strings are written through
// such references while they are built (string.Create, spans, fixed), so the string must neither be merged into another one, because the
// written string would not be the one that the rewritten reference leads to, nor become the canonical string of its content, because the
// references that are merged into it would see it change. GCHeap::Promote notes these strings while the GC marks (string_dedup_note_root).
//
// The pair is dropped when the GC is going to leave the canonical string in a younger generation than gen2, because the finder removes it
// from its table then. The finder does not record a pair for an entry of the queue that is left from a GC that could not consume it (the table
// could not be allocated then): it is in gen2, which a GC of gen1 does not condemn, so this GC would not see all the references to it.
//
// The result is reported with the StringDedupMerge GC event, once for every blocking GC that ran the finder. The payload of its version 2 is
// these uint64 values in order:
//   GC index, status (0 the relocate phase ran, 1 the GC did not compact, 2 a background GC was in progress, whether or not the GC compacted,
//   3 out of memory),
//   condemned generation, duplicates that the finder recorded as pairs (all of its duplicates), candidates left out because a stack, a
//   register or a byref refers to them, pairs dropped when they were registered, pairs that were registered, registered duplicates whose
//   references were replaced, their size in bytes, references that were replaced, registered duplicates that no reference was found for (that
//   must be 0, every duplicate is marked so something refers to it), time that the merge itself spent in microseconds (the cost of looking at
//   every reference that the relocate phase relocates is in the pause only), 1 if the GC compacted and 0 if it did not, the size in bytes of the
//   duplicates that the finder recorded as pairs, 1 if the GC compacted only because string_dedup_compaction_pays asked for it.

struct string_dedup_pair
{
    uint8_t* duplicate;
    uint8_t* canonical;  // where the canonical string is going to be after the GC, nullptr for a pair that was dropped
    size_t bytes;        // the size of the duplicate
    size_t references;   // the references to the duplicate that the relocate phase replaced
};

struct string_dedup_merge_stats
{
    uint64_t gc_index;
    uint64_t status;
    uint64_t condemned_generation;
    uint64_t pairs;
    uint64_t skipped_rooted;
    uint64_t dropped;
    uint64_t registered;
    uint64_t merged;
    uint64_t merged_bytes;
    uint64_t references;
    uint64_t unreferenced;
    uint64_t elapsed_us;
    uint64_t compacted;
    uint64_t pair_bytes;
    uint64_t upgraded;
};

// What relocate_address looks at while the merge is active: the registered pairs in an open addressing table that is two thirds full at most, a pair
// in the slot that the address of its duplicate leads to (an empty slot has no duplicate), so that finding a pair takes one cache line, and a filter with
// 16 bits for every pair, which tells most other addresses apart with one load.
struct string_dedup_redirects
{
    string_dedup_pair* slots;
    size_t slot_count;
    size_t slot_capacity; // what is allocated, the buffers are kept for the next GC that merges
    uint64_t* filter;
    size_t filter_bits;   // a power of two
    size_t filter_capacity;
};

#endif //USE_REGIONS

#endif //!MULTIPLE_HEAPS

// The queue of one heap with the statistics of the GC in progress.
struct string_dedup_queue_state
{
    string_dedup_candidate* entries;
    size_t count;
    size_t first_new; // the entries from this index on were queued by the blocking GC in progress
    string_dedup_queue_stats gc;
#ifndef MULTIPLE_HEAPS
    string_dedup_table* table;
    string_dedup_find_stats find;
#ifdef USE_REGIONS
    string_dedup_pair* pairs; // the duplicates that the GC in progress found, nullptr when the merge is off
    size_t pair_count;
    string_dedup_merge_stats merge;
    string_dedup_redirects redirects;
#endif //USE_REGIONS
#endif //!MULTIPLE_HEAPS
};

#ifdef MULTIPLE_HEAPS
const size_t string_dedup_queue_capacity = (size_t)1 << 14;
#else //MULTIPLE_HEAPS
const size_t string_dedup_queue_capacity = (size_t)1 << 16;

// The merge works on the pairs that the finder records, so it implies the finder, and it needs regions.
static inline bool string_dedup_merge_enabled()
{
#ifdef USE_REGIONS
    return GCConfig::GetStringDedupMerge() != 0;
#else //USE_REGIONS
    return false;
#endif //USE_REGIONS
}

static inline bool string_dedup_find_enabled()
{
    return (GCConfig::GetStringDedupFind() != 0) || string_dedup_merge_enabled();
}
#endif //MULTIPLE_HEAPS

// The tests that the passes over the queue and the table make for each of their entries. The passes spend their time in them, so they are
// inlined instead of going through the GC interface (GCHeap::IsPromoted and GCHeap::WhichGeneration), which costs more than the memory
// access that the test is about.
enum string_dedup_check
{
    string_dedup_check_marked,     // marked by the mark phase of a blocking GC that condemns gen2, before it plans
#ifdef BACKGROUND_GC
    string_dedup_check_bgc_marked, // marked by the final mark of a background GC
#endif //BACKGROUND_GC
    string_dedup_check_in_gen2     // in gen2 after the GC has planned the heap
};

struct string_dedup_test
{
    static inline gc_heap* any_heap()
    {
#ifdef MULTIPLE_HEAPS
        return gc_heap::g_heaps[0];
#else //MULTIPLE_HEAPS
        return (gc_heap*)0;
#endif //MULTIPLE_HEAPS
    }

    // The same as GCHeap::IsPromoted in a blocking GC that condemns gen2: whatever is outside of the range of the GC is alive.
    static inline bool alive_after_mark (uint8_t* obj)
    {
        gc_heap* hp = any_heap();

        return !((obj < hp->highest_address) && (obj >= hp->lowest_address)) || hp->is_mark_set (obj);
    }

#ifdef BACKGROUND_GC
    // The same as GCHeap::IsPromoted in a background GC.
    static inline bool alive_after_bgc_mark (uint8_t* obj)
    {
        gc_heap* hp = any_heap();

        return !((obj < hp->background_saved_highest_address) && (obj >= hp->background_saved_lowest_address)) || hp->background_marked (obj);
    }
#endif //BACKGROUND_GC

    // The same as GCHeap::WhichGeneration (obj) == max_generation.
    static inline bool in_gen2 (uint8_t* obj)
    {
#ifdef USE_REGIONS
        return (obj >= g_gc_lowest_address) && (obj < g_gc_highest_address) &&
               ((gc_heap::map_region_to_generation_skewed[get_skewed_basic_region_index_for_address (obj)] & gc_heap::RI_GEN_MASK) == max_generation);
#else //USE_REGIONS
        return g_theGCHeap->WhichGeneration ((Object*)obj) == max_generation;
#endif //USE_REGIONS
    }
};

// Whether an entry passes the check, the others are removed.
static inline bool string_dedup_entry_passes (string_dedup_check check, uint8_t* obj)
{
    switch (check)
    {
        case string_dedup_check_marked:
            return string_dedup_test::alive_after_mark (obj);
#ifdef BACKGROUND_GC
        case string_dedup_check_bgc_marked:
            return string_dedup_test::alive_after_bgc_mark (obj);
#endif //BACKGROUND_GC
        default:
            assert (check == string_dedup_check_in_gen2);
            return string_dedup_test::in_gen2 (obj);
    }
}

static uint64_t string_dedup_signature (CObjectHeader* str)
{
    uint32_t length = str->GetNumComponents();
    uint32_t first_chars = 0;
    memcpy (&first_chars, (uint8_t*)str + string_dedup_chars_offset, ((length < 2) ? length : 2) * sizeof (uint16_t));

    return ((uint64_t)length << 32) | first_chars;
}

static bool string_dedup_queue_reserve (string_dedup_queue_state* queue)
{
    if (queue->entries == nullptr)
    {
        queue->entries = new (nothrow) string_dedup_candidate[string_dedup_queue_capacity];
    }

#if defined(USE_REGIONS) && !defined(MULTIPLE_HEAPS)
    // Every candidate leads to at most one pair, so the pairs never need more room than the queue.
    if ((queue->pairs == nullptr) && string_dedup_merge_enabled())
    {
        queue->pairs = new (nothrow) string_dedup_pair[string_dedup_queue_capacity];
    }
#endif //USE_REGIONS && !MULTIPLE_HEAPS

    return queue->entries != nullptr;
}

static void string_dedup_queue_add (CObjectHeader* str, string_dedup_queue_state* queue)
{
    string_dedup_queue_stats* stats = &queue->gc;
    stats->seen++;

    if (str->IsPinned())
    {
        stats->skipped_pinned++;
    }
    else if (string_dedup_header_has_state (str))
    {
        stats->skipped_header++;
    }
    else if (queue->count == string_dedup_queue_capacity)
    {
        stats->dropped++;
    }
    else
    {
        string_dedup_candidate* candidate = &queue->entries[queue->count++];
        candidate->obj = (uint8_t*)str;
        candidate->signature = string_dedup_signature (str);
        stats->enqueued++;
    }
}

// Queues the marked strings of a range of the small object heap, the objects are parsed by size because the heap is not planned yet.
static void string_dedup_queue_add_range (uint8_t* start, uint8_t* end, string_dedup_queue_state* queue)
{
    uint8_t* o = start;
    while (o < end)
    {
        CObjectHeader* obj = (CObjectHeader*)o;
        if (obj->IsMarked() && string_dedup_is_string (obj->GetMethodTable()))
        {
            string_dedup_queue_add (obj, queue);
        }

        o += Align (size (o));
    }
}

// Removes the entries that do not pass the check, the others keep their order. Returns the number of removed entries.
static size_t string_dedup_queue_remove_if (string_dedup_queue_state* queue, string_dedup_check check)
{
    size_t kept = 0;
    for (size_t i = 0; i < queue->count; i++)
    {
        if (string_dedup_entry_passes (check, queue->entries[i].obj))
        {
            queue->entries[kept++] = queue->entries[i];
        }
    }

    size_t removed = queue->count - kept;
    queue->count = kept;

    return removed;
}

static void string_dedup_queue_fire (const string_dedup_queue_stats* stats, size_t queue_size)
{
    if (!GCEventEnabledStringDedupQueue_V3())
    {
        return;
    }

    GCEventFireStringDedupQueue_V3 (
        stats->gc_index,
        stats->status,
        stats->condemned_generation,
        stats->seen,
        stats->enqueued,
        stats->dropped,
        stats->skipped_pinned,
        stats->skipped_header,
        stats->died,
        stats->relocated,
        (uint64_t)queue_size,
        stats->elapsed_us,
        stats->heap_index,
        stats->demoted,
        stats->background
    );
}

#ifndef MULTIPLE_HEAPS

// The number of slots of the index for a table that holds 'capacity' strings: a power of two with room for twice as many strings, or for
// four thirds as many at the largest size.
static size_t string_dedup_table_slot_count (const string_dedup_table* table, size_t capacity)
{
    size_t slot_count = 1;
    while ((slot_count < (capacity * 2)) && (slot_count < table->max_slot_count))
    {
        slot_count *= 2;
    }

    return slot_count;
}

// Rebuilds the table with another capacity from its live strings, which drops the removed ones and keeps the order of the others. The
// capacity has to hold all of them. Returns false and leaves the table as it is when memory ran out.
static bool string_dedup_table_rebuild (string_dedup_table* table, size_t capacity)
{
    assert ((capacity >= table->live) && (capacity <= table->max_capacity));

    size_t slot_count = string_dedup_table_slot_count (table, capacity);
    uint8_t** strings = new (nothrow) uint8_t*[capacity];
    uint32_t* hashes = new (nothrow) uint32_t[capacity];
    string_dedup_slot* slots = new (nothrow) string_dedup_slot[slot_count];
    if ((strings == nullptr) || (hashes == nullptr) || (slots == nullptr))
    {
        delete[] strings;
        delete[] hashes;
        delete[] slots;

        return false;
    }

    memset (slots, 0, slot_count * sizeof (string_dedup_slot));
    size_t count = 0;
    for (size_t i = 0; i < table->count; i++)
    {
        if (table->strings[i] != nullptr)
        {
            uint32_t hash = table->hashes[i];
            strings[count] = table->strings[i];
            hashes[count] = hash;

            size_t index = (size_t)hash & (slot_count - 1);
            while (slots[index].entry != 0)
            {
                index = (index + 1) & (slot_count - 1);
            }

            slots[index].entry = (uint32_t)(count + 1);
            slots[index].hash = hash;
            count++;
        }
    }

    delete[] table->strings;
    delete[] table->hashes;
    delete[] table->slots;
    table->strings = strings;
    table->hashes = hashes;
    table->slots = slots;
    table->count = count;
    table->capacity = capacity;
    table->slot_count = slot_count;

    return true;
}

static string_dedup_table* string_dedup_table_create()
{
    string_dedup_table* table = new (nothrow) string_dedup_table();
    if (table != nullptr)
    {
        table->seed = GetHighPrecisionTimeStamp();

        int64_t bits = GCConfig::GetStringDedupFindTableBits();
        if (bits < string_dedup_table_min_bits)
        {
            bits = string_dedup_table_min_bits;
        }
        else if (bits > string_dedup_table_max_bits)
        {
            bits = string_dedup_table_max_bits;
        }

        table->max_slot_count = (size_t)1 << bits;
        table->max_capacity = (table->max_slot_count / 4) * 3;
        if (!string_dedup_table_rebuild (table, string_dedup_table_initial_capacity))
        {
            delete table;
            table = nullptr;
        }
    }

    return table;
}

// Called when a new string is going to be added to a table whose arrays are full. Every string that was ever added since the table was
// rebuilt has a slot in the index, so the arrays being full keeps the index at most half full, or three quarters at the largest size, which
// keeps probing short and makes it end at an empty slot. Many removed strings are dropped by rebuilding at the same size, otherwise the table
// has to grow. Returns false when that is not possible because the table is at its largest size or memory ran out. The strings and the
// slots have moved when it returns true.
static bool string_dedup_table_make_room (string_dedup_table* table)
{
    assert (table->count == table->capacity);

    if ((table->count - table->live) > (table->capacity / 8))
    {
        return string_dedup_table_rebuild (table, table->capacity);
    }

    if (table->capacity < table->max_capacity)
    {
        size_t capacity = table->capacity * 2;

        return string_dedup_table_rebuild (table, (capacity < table->max_capacity) ? capacity : table->max_capacity);
    }

    return false;
}

// Rebuilds a table that has been emptied by removing strings, at a much smaller size or without the holes that they left, so that the
// passes over it stay cheap.
static void string_dedup_table_shrink (string_dedup_table* table)
{
    size_t capacity = string_dedup_table_initial_capacity;
    while (capacity < (table->live * 2))
    {
        capacity *= 2;
    }

    if (capacity > table->capacity)
    {
        capacity = table->capacity;
    }

    size_t holes = table->count - table->live;
    if (((capacity * 4) <= table->capacity) || ((holes > 0) && (holes >= table->live)))
    {
        string_dedup_table_rebuild (table, capacity);
    }
}

static uint32_t string_dedup_table_hash (const string_dedup_table* table, CObjectHeader* str)
{
    uint64_t hash = string_dedup_hash ((uint8_t*)str + string_dedup_chars_offset, (size_t)str->GetNumComponents() * sizeof (uint16_t), table->seed);

    return (uint32_t)(hash ^ (hash >> 32));
}

static bool string_dedup_equal (CObjectHeader* a, CObjectHeader* b, uint32_t length)
{
    return (a->GetNumComponents() == length) &&
           (memcmp ((uint8_t*)a + string_dedup_chars_offset, (uint8_t*)b + string_dedup_chars_offset, (size_t)length * sizeof (uint16_t)) == 0);
}

// Returns the slot of the canonical string with the content of 'str', or nullptr when there is none. Then 'free_slot' is the empty slot that
// ended the search, where a new string goes. The slots of removed strings are skipped, they stay in use until the table is rebuilt.
static string_dedup_slot* string_dedup_table_find (string_dedup_table* table, uint32_t hash, CObjectHeader* str, uint32_t length, size_t* free_slot)
{
    size_t mask = table->slot_count - 1;
    size_t index = (size_t)hash & mask;
    while (table->slots[index].entry != 0)
    {
        string_dedup_slot* slot = &table->slots[index];
        if (slot->hash == hash)
        {
            uint8_t* canonical = table->strings[slot->entry - 1];
            if ((canonical != nullptr) && string_dedup_equal ((CObjectHeader*)canonical, str, length))
            {
                return slot;
            }
        }

        index = (index + 1) & mask;
    }

    *free_slot = index;

    return nullptr;
}

// Removes the strings from position 'first' on that do not pass the check. Returns the number of removed strings.
static size_t string_dedup_table_remove_if (string_dedup_table* table, string_dedup_check check, size_t first)
{
    size_t removed = 0;
    for (size_t i = first; i < table->count; i++)
    {
        uint8_t* obj = table->strings[i];
        if ((obj != nullptr) && !string_dedup_entry_passes (check, obj))
        {
            table->strings[i] = nullptr;
            removed++;
        }
    }

    table->live -= removed;

    return removed;
}

static void string_dedup_table_relocate (string_dedup_table* table, ScanContext* sc, size_t first)
{
    for (size_t i = first; i < table->count; i++)
    {
        if (table->strings[i] != nullptr)
        {
            GCHeap::Relocate ((Object**)&table->strings[i], sc);
        }
    }
}

// The strings that the finder added in the GC in progress. It appends them and a rebuild keeps the order, so they are the last ones.
static size_t string_dedup_table_added_start (const string_dedup_table* table, const string_dedup_find_stats* stats)
{
    return table->count - (size_t)stats->added;
}

#ifdef USE_REGIONS

// The slot where the search for a pointer starts in a table of 'mask + 1' slots, a power of two.
static inline size_t string_dedup_pointer_slot (uint8_t* p, size_t mask)
{
    return (size_t)(((uint64_t)(size_t)p * 0x9E3779B97F4A7C15ull) >> 32) & mask;
}

// The strings that a stack, a register or a byref refers to, noted by GCHeap::Promote while the GC marks. The set is empty after every GC that
// ran the finder, so it only holds what the GC in progress has seen. When memory runs out some of them are not in it and the GC does not merge.
struct string_dedup_rooted_set
{
    uint8_t** slots;    // nullptr for an empty slot
    size_t slot_count;  // a power of two, 0 until the first string is added
    size_t count;
    bool incomplete;
};

static string_dedup_rooted_set string_dedup_rooted;

static void string_dedup_rooted_add (uint8_t* o)
{
    string_dedup_rooted_set* set = &string_dedup_rooted;
    if ((set->count + 1) * 2 > set->slot_count)
    {
        size_t slot_count = (set->slot_count == 0) ? 1024 : (set->slot_count * 2);
        uint8_t** slots = new (nothrow) uint8_t*[slot_count];
        if (slots == nullptr)
        {
            set->incomplete = true;

            return;
        }

        memset (slots, 0, slot_count * sizeof (uint8_t*));
        for (size_t i = 0; i < set->slot_count; i++)
        {
            if (set->slots[i] != nullptr)
            {
                size_t index = string_dedup_pointer_slot (set->slots[i], slot_count - 1);
                while (slots[index] != nullptr)
                {
                    index = (index + 1) & (slot_count - 1);
                }

                slots[index] = set->slots[i];
            }
        }

        delete[] set->slots;
        set->slots = slots;
        set->slot_count = slot_count;
    }

    size_t index = string_dedup_pointer_slot (o, set->slot_count - 1);
    while (set->slots[index] != nullptr)
    {
        if (set->slots[index] == o)
        {
            return;
        }

        index = (index + 1) & (set->slot_count - 1);
    }

    set->slots[index] = o;
    set->count++;
}

static bool string_dedup_rooted_contains (uint8_t* o)
{
    string_dedup_rooted_set* set = &string_dedup_rooted;
    if (set->count == 0)
    {
        return false;
    }

    for (size_t index = string_dedup_pointer_slot (o, set->slot_count - 1); set->slots[index] != nullptr; index = (index + 1) & (set->slot_count - 1))
    {
        if (set->slots[index] == o)
        {
            return true;
        }
    }

    return false;
}

// Empties the set. A set that has grown large is given back, a GC with many roots is rare.
static void string_dedup_rooted_clear()
{
    string_dedup_rooted_set* set = &string_dedup_rooted;
    if (set->slot_count > ((size_t)1 << 16))
    {
        delete[] set->slots;
        set->slots = nullptr;
        set->slot_count = 0;
    }
    else if (set->count != 0)
    {
        memset (set->slots, 0, set->slot_count * sizeof (uint8_t*));
    }

    set->count = 0;
    set->incomplete = false;
}

// Records that 'duplicate' has the content of the canonical string. There is room for a pair for every candidate of the queue.
static void string_dedup_pair_add (string_dedup_queue_state* queue, uint8_t* duplicate, uint8_t* canonical, size_t bytes)
{
    if ((queue->pairs == nullptr) || string_dedup_rooted.incomplete)
    {
        return;
    }

    assert (queue->pair_count < string_dedup_queue_capacity);
    queue->pairs[queue->pair_count++] = string_dedup_pair { duplicate, canonical, bytes, 0 };
    queue->merge.pairs++;
    queue->merge.pair_bytes += bytes;
}

#endif //USE_REGIONS

// Takes the candidates out of the queue. A string whose content is not in the table becomes the canonical string for it, any other string
// is a duplicate of the canonical one.
static void string_dedup_find_consume (string_dedup_queue_state* queue, string_dedup_table* table, string_dedup_find_stats* stats)
{
    for (size_t i = 0; i < queue->count; i++)
    {
        CObjectHeader* str = (CObjectHeader*)queue->entries[i].obj;
        stats->candidates++;

#ifdef USE_REGIONS
        // A string that a stack, a register or a byref refers to can be under construction. It is not merged into another string, and it does
        // not become the canonical string of its content either, because the references that are merged into it would see it change.
        if ((queue->pairs != nullptr) && string_dedup_rooted_contains ((uint8_t*)str))
        {
            queue->merge.skipped_rooted++;
            continue;
        }
#endif //USE_REGIONS
        // The identity of the string may have become observable since it was queued.
        if (string_dedup_header_has_state (str))
        {
            stats->skipped_header++;
            continue;
        }

        uint32_t length = str->GetNumComponents();
        uint32_t hash = string_dedup_table_hash (table, str);
        size_t free_slot = 0;
        string_dedup_slot* canonical = string_dedup_table_find (table, hash, str, length, &free_slot);
        if (canonical != nullptr)
        {
            if (table->strings[canonical->entry - 1] != (uint8_t*)str)
            {
                size_t bytes = Align (size ((uint8_t*)str));
                stats->duplicates++;
                stats->duplicate_bytes += bytes;
#ifdef USE_REGIONS
                // The entries from before 'first_new' are left from a GC that could not consume them, they are in gen2 and a gen1 GC does not see all
                // the references to them.
                if (i >= queue->first_new)
                {
                    string_dedup_pair_add (queue, (uint8_t*)str, table->strings[canonical->entry - 1], bytes);
                }
#endif //USE_REGIONS
            }

            continue;
        }

        // Making room moves the slots, then the one that is free has to be searched again.
        if (table->count == table->capacity)
        {
            if (!string_dedup_table_make_room (table))
            {
                stats->not_added++;
                continue;
            }

            string_dedup_table_find (table, hash, str, length, &free_slot);
        }

        table->strings[table->count] = (uint8_t*)str;
        table->hashes[table->count] = hash;
        table->slots[free_slot].entry = (uint32_t)(table->count + 1);
        table->slots[free_slot].hash = hash;
        table->count++;
        table->live++;
        stats->added++;
    }

    queue->count = 0;
}

static void string_dedup_find_fire (const string_dedup_find_stats* stats, size_t table_size)
{
    if (!GCEventEnabledStringDedupFind_V1())
    {
        return;
    }

    GCEventFireStringDedupFind_V1 (
        stats->gc_index,
        stats->status,
        stats->condemned_generation,
        stats->candidates,
        stats->skipped_header,
        stats->duplicates,
        stats->duplicate_bytes,
        stats->added,
        stats->not_added,
        stats->died,
        stats->demoted,
        (uint64_t)table_size,
        stats->elapsed_us,
        stats->background
    );
}

#ifdef USE_REGIONS

// The first slot of the search for a duplicate in a table of 'slot_count' slots. The strings of a size are at a regular distance, which a plain
// multiplication turns into clusters for some distances, so the high bits are folded into the low ones, which are reduced to the range by multiplying
// so the table does not have to have a power of two slots.
static inline size_t string_dedup_redirect_slot_of (uint8_t* p, size_t slot_count)
{
    uint64_t hash = (uint64_t)(size_t)p * 0x9E3779B97F4A7C15ull;
    hash ^= hash >> 32;

    return (size_t)(((hash & 0xFFFFFFFFull) * slot_count) >> 32);
}

// The bit of a duplicate in the filter, which has 'filter_bits' bits, a power of two.
static inline size_t string_dedup_redirect_filter_bit (uint8_t* p, size_t filter_bits)
{
    return (size_t)(((uint64_t)(size_t)p * 0xC2B2AE3D27D4EB4Full) >> 40) & (filter_bits - 1);
}

// Fills the map with the registered pairs, the ones that have a canonical string, which it copies, in buffers that it keeps for the next time.
// Returns false when memory runs out.
static bool string_dedup_redirects_create (string_dedup_redirects* map, const string_dedup_pair* pairs, size_t pair_count, size_t registered)
{
    size_t slot_count = registered + (registered / 2) + 16;
    size_t filter_bits = 1024;
    while (filter_bits < (registered * 16))
    {
        filter_bits *= 2;
    }

    if (map->slot_capacity < slot_count)
    {
        delete[] map->slots;
        map->slots = new (nothrow) string_dedup_pair[slot_count];
        map->slot_capacity = (map->slots != nullptr) ? slot_count : 0;
    }

    if (map->filter_capacity < filter_bits)
    {
        delete[] map->filter;
        map->filter = new (nothrow) uint64_t[filter_bits / 64];
        map->filter_capacity = (map->filter != nullptr) ? filter_bits : 0;
    }

    if ((map->slot_capacity < slot_count) || (map->filter_capacity < filter_bits))
    {
        return false;
    }

    memset (map->slots, 0, slot_count * sizeof (string_dedup_pair));
    memset (map->filter, 0, filter_bits / 8);
    map->slot_count = slot_count;
    map->filter_bits = filter_bits;
    for (size_t i = 0; i < pair_count; i++)
    {
        if (pairs[i].canonical == nullptr)
        {
            continue;
        }

        size_t index = string_dedup_redirect_slot_of (pairs[i].duplicate, slot_count);
        while (map->slots[index].duplicate != nullptr)
        {
            index = (index + 1 == slot_count) ? 0 : (index + 1);
        }

        map->slots[index] = pairs[i];

        size_t bit = string_dedup_redirect_filter_bit (pairs[i].duplicate, filter_bits);
        map->filter[bit / 64] |= (uint64_t)1 << (bit % 64);
    }

    return true;
}

// The pair of a registered duplicate, nullptr for any other address.
static inline string_dedup_pair* string_dedup_redirects_find (const string_dedup_redirects* map, uint8_t* o)
{
    size_t bit = string_dedup_redirect_filter_bit (o, map->filter_bits);
    if ((map->filter[bit / 64] & ((uint64_t)1 << (bit % 64))) == 0)
    {
        return nullptr;
    }

    for (size_t index = string_dedup_redirect_slot_of (o, map->slot_count); map->slots[index].duplicate != nullptr; index = (index + 1 == map->slot_count) ? 0 : (index + 1))
    {
        if (map->slots[index].duplicate == o)
        {
            return &map->slots[index];
        }
    }

    return nullptr;
}

static void string_dedup_merge_fire (const string_dedup_merge_stats* stats)
{
    if (!GCEventEnabledStringDedupMerge_V2())
    {
        return;
    }

    GCEventFireStringDedupMerge_V2 (
        stats->gc_index,
        stats->status,
        stats->condemned_generation,
        stats->pairs,
        stats->skipped_rooted,
        stats->dropped,
        stats->registered,
        stats->merged,
        stats->merged_bytes,
        stats->references,
        stats->unreferenced,
        stats->elapsed_us,
        stats->compacted,
        stats->pair_bytes,
        stats->upgraded
    );
}

#endif //USE_REGIONS

// Runs right after marking in a blocking GC that condemns gen1, when the queue has been filled and the plan phase has not changed anything.
static void string_dedup_find_after_mark (string_dedup_queue_state* queue)
{
    string_dedup_find_stats* stats = &queue->find;
    uint64_t start_time = GetHighPrecisionTimeStamp();

    if (queue->table == nullptr)
    {
        queue->table = string_dedup_table_create();
    }

    if (queue->table == nullptr)
    {
        stats->status = 1;
    }
    else
    {
        string_dedup_find_consume (queue, queue->table, stats);

        // This also consumed the entries that an earlier GC could not, because the table could not be created then, so none is old anymore.
        queue->first_new = 0;
    }

    stats->elapsed_us += GetHighPrecisionTimeStamp() - start_time;
}

// Runs after the plan phase of a GC that ran the finder or that can have left strings of the table in a younger generation.
static void string_dedup_find_report (string_dedup_queue_state* queue, bool queued, bool check_all)
{
    string_dedup_find_stats* stats = &queue->find;
    string_dedup_table* table = queue->table;

    if (table != nullptr)
    {
        uint64_t start_time = GetHighPrecisionTimeStamp();
        size_t first = check_all ? 0 : string_dedup_table_added_start (table, stats);
        stats->demoted = string_dedup_table_remove_if (table, string_dedup_check_in_gen2, first);
        stats->elapsed_us += GetHighPrecisionTimeStamp() - start_time;
    }

    if (queued || (stats->demoted != 0))
    {
        string_dedup_find_fire (stats, (table != nullptr) ? table->live : 0);
    }
}

#endif //!MULTIPLE_HEAPS

bool gc_heap::string_dedup_queue_enabled()
{
#ifdef MULTIPLE_HEAPS
    return GCConfig::GetStringDedupQueue();
#else //MULTIPLE_HEAPS
    return GCConfig::GetStringDedupQueue() || string_dedup_find_enabled();
#endif //MULTIPLE_HEAPS
}

// Runs right after marking in a blocking GC that condemns gen1.
void gc_heap::string_dedup_queue_after_mark (int condemned_gen_number)
{
    if (string_dedup_queue == nullptr)
    {
        string_dedup_queue = new (nothrow) string_dedup_queue_state();
    }

    string_dedup_queue_state* queue = string_dedup_queue;
    if (queue != nullptr)
    {
        queue->gc = {};
        queue->gc.gc_index = (uint64_t)settings.gc_index;
        queue->gc.condemned_generation = (uint64_t)condemned_gen_number;
        queue->gc.heap_index = (uint64_t)heap_number;
        queue->gc.report_pending = true;

#ifndef MULTIPLE_HEAPS
        queue->find = {};
        queue->find.gc_index = (uint64_t)settings.gc_index;
        queue->find.condemned_generation = (uint64_t)condemned_gen_number;
#ifdef USE_REGIONS
        queue->pair_count = 0;
        queue->merge = {};
        queue->merge.gc_index = (uint64_t)settings.gc_index;
        queue->merge.condemned_generation = (uint64_t)condemned_gen_number;
#endif //USE_REGIONS

        if ((condemned_gen_number == max_generation) && (queue->table != nullptr))
        {
            assert (!settings.concurrent);
            uint64_t start_time = GetHighPrecisionTimeStamp();
            queue->find.died = string_dedup_table_remove_if (queue->table, string_dedup_check_marked, 0);
            string_dedup_table_shrink (queue->table);
            queue->find.elapsed_us = GetHighPrecisionTimeStamp() - start_time;
        }
#endif //!MULTIPLE_HEAPS

        if (!string_dedup_queue_reserve (queue))
        {
            queue->gc.status = 1;
        }
        else
        {
            uint64_t start_time = GetHighPrecisionTimeStamp();

            if (condemned_gen_number == max_generation)
            {
                queue->gc.died = string_dedup_queue_remove_if (queue, string_dedup_check_marked);
            }

            queue->first_new = queue->count;

            if (settings.promotion)
            {
#ifdef USE_REGIONS
                for (heap_segment* region = generation_start_segment (generation_of (1)); region != nullptr; region = heap_segment_next (region))
                {
                    string_dedup_queue_add_range (heap_segment_mem (region), heap_segment_allocated (region), queue);
                }
#else //USE_REGIONS
                string_dedup_queue_add_range (generation_allocation_start (generation_of (1)), generation_allocation_start (generation_of (0)), queue);
#endif //USE_REGIONS
            }

            queue->gc.elapsed_us = GetHighPrecisionTimeStamp() - start_time;

#ifndef MULTIPLE_HEAPS
            if (string_dedup_find_enabled())
            {
#ifdef USE_REGIONS
                if (string_dedup_merge_enabled() && ((queue->pairs == nullptr) || string_dedup_rooted.incomplete))
                {
                    queue->merge.status = 3;
                }
#endif //USE_REGIONS

                string_dedup_find_after_mark (queue);
            }
#endif //!MULTIPLE_HEAPS
        }
    }

#if defined(USE_REGIONS) && !defined(MULTIPLE_HEAPS)
    // The strings that the roots noted have been used by now.
    string_dedup_rooted_clear();
#endif //USE_REGIONS && !MULTIPLE_HEAPS

#ifdef MULTIPLE_HEAPS
    // Planning overwrites dead objects, so no heap may start it before every heap has checked its entries, which can point into other
    // heaps. All heaps have to get here, whatever happened above.
    gc_t_join.join (this, gc_join_string_dedup_queue);
    if (gc_t_join.joined())
    {
        gc_t_join.restart();
    }
#endif //MULTIPLE_HEAPS
}

// Runs in the relocate phase of a compacting GC, like the relocation of the finalization queue.
void gc_heap::string_dedup_queue_relocate()
{
    string_dedup_queue_state* queue = string_dedup_queue;
    if ((queue == nullptr) || !queue->gc.report_pending)
    {
        return;
    }

    ScanContext sc;
    sc.promotion = FALSE;
#ifdef MULTIPLE_HEAPS
    sc.thread_number = heap_number;
    sc.thread_count = n_heaps;
#else //MULTIPLE_HEAPS
    sc.thread_count = 1;
#endif //MULTIPLE_HEAPS

    // A gen1 GC does not move the strings of gen2, so it only relocates the entries it queued itself.
    bool gen2_p = (settings.condemned_generation == max_generation);
    uint64_t start_time = GetHighPrecisionTimeStamp();

    for (size_t i = (gen2_p ? 0 : queue->first_new); i < queue->count; i++)
    {
        uint8_t* old_address = queue->entries[i].obj;
        GCHeap::Relocate ((Object**)&queue->entries[i].obj, &sc);
        if (queue->entries[i].obj != old_address)
        {
            queue->gc.relocated++;
        }
    }

    queue->gc.elapsed_us += GetHighPrecisionTimeStamp() - start_time;

#ifndef MULTIPLE_HEAPS
    if (queue->table != nullptr)
    {
        start_time = GetHighPrecisionTimeStamp();
        string_dedup_table_relocate (queue->table, &sc, gen2_p ? 0 : string_dedup_table_added_start (queue->table, &queue->find));
        queue->find.elapsed_us += GetHighPrecisionTimeStamp() - start_time;
    }
#endif //!MULTIPLE_HEAPS
}

#if defined(USE_REGIONS) && !defined(MULTIPLE_HEAPS)

// Called by GCHeap::Promote for the roots that are on a stack or in a register and for every byref, with the object that they lead to.
void gc_heap::string_dedup_note_root (uint8_t* o)
{
    // A GC that does not condemn gen1 does not look for duplicates.
    if (!string_dedup_merge_enabled() || (settings.condemned_generation < 1) || !string_dedup_is_string (((CObjectHeader*)o)->GetMethodTable()))
    {
        return;
    }

    string_dedup_rooted_add (o);
}

// Runs at the start of the relocate phase of a GC that compacts, before any reference is relocated, and registers the pairs that can be merged.
// The duplicate has to be in a region that this GC condemns, because only then does it see every reference to it, which holds for the strings that
// leave gen1, and the canonical string has to end up in gen2 without being demoted, because it is removed from the table otherwise.
void gc_heap::string_dedup_merge_begin()
{
    string_dedup_queue_state* queue = string_dedup_queue;
    if ((queue == nullptr) || (queue->pairs == nullptr) || !queue->gc.report_pending)
    {
        return;
    }

    string_dedup_merge_stats* stats = &queue->merge;
    if (stats->status != 0)
    {
        return;
    }

#ifdef BACKGROUND_GC
    if (background_running_p())
    {
        stats->status = 2;

        return;
    }
#endif //BACKGROUND_GC

    if (queue->pair_count == 0)
    {
        return;
    }

    uint64_t start_time = GetHighPrecisionTimeStamp();

    // The duplicates of a content are often together, so the canonical string of the previous pair is likely to be the one of this pair.
    uint8_t* last_canonical = nullptr;
    uint8_t* last_relocated = nullptr;
    size_t registered = 0;
    for (size_t i = 0; i < queue->pair_count; i++)
    {
        string_dedup_pair* pair = &queue->pairs[i];
        assert (get_region_gen_num (pair->duplicate) <= settings.condemned_generation);
        if (pair->canonical != last_canonical)
        {
            last_canonical = pair->canonical;
            last_relocated = last_canonical;
            relocate_address (&last_relocated);
            if ((get_region_plan_gen_num (last_relocated) != max_generation) || is_region_demoted (last_relocated))
            {
                last_relocated = nullptr;
            }
        }

        uint8_t* canonical = last_relocated;
        pair->canonical = canonical;
        if (canonical != nullptr)
        {
            registered++;
        }
        else
        {
            stats->dropped++;
        }
    }

    stats->registered = registered;
    if (registered != 0)
    {
        if (!string_dedup_redirects_create (&queue->redirects, queue->pairs, queue->pair_count, registered))
        {
            stats->status = 3;
            stats->registered = 0;
        }
        else
        {
            // From now on relocate_address replaces the references to the registered duplicates.
            string_dedup_redirect_map = &queue->redirects;
        }
    }

    stats->elapsed_us += GetHighPrecisionTimeStamp() - start_time;
}

// Called by relocate_address for every reference that it relocates while the merge is active. Returns true when it replaced the reference.
bool gc_heap::string_dedup_redirect (uint8_t** pold_address)
{
    string_dedup_pair* pair = string_dedup_redirects_find (string_dedup_redirect_map, *pold_address);
    if (pair == nullptr)
    {
        return false;
    }

    *pold_address = pair->canonical;
    pair->references++;

    return true;
}

// Runs at the end of the relocate phase of a GC that compacts: the references are replaced, so the registrations are dropped and the
// duplicates are counted.
void gc_heap::string_dedup_merge_end()
{
    string_dedup_redirects* map = string_dedup_redirect_map;
    if (map == nullptr)
    {
        return;
    }

    string_dedup_redirect_map = nullptr;

    string_dedup_queue_state* queue = string_dedup_queue;
    string_dedup_merge_stats* stats = &queue->merge;
    uint64_t start_time = GetHighPrecisionTimeStamp();

    for (size_t i = 0; i < map->slot_count; i++)
    {
        string_dedup_pair* pair = &map->slots[i];
        if (pair->duplicate == nullptr)
        {
            continue;
        }

        if (pair->references != 0)
        {
            stats->merged++;
            stats->merged_bytes += pair->bytes;
            stats->references += pair->references;
        }
        else
        {
            stats->unreferenced++;
        }
    }

    stats->elapsed_us += GetHighPrecisionTimeStamp() - start_time;
}

// Called by decide_on_compacting for a GC that is going to sweep, after every other reason to compact. A sweeping GC merges nothing, so a gen1 GC
// compacts when the duplicates that the finder recorded in it are worth GCStringDedupCompactMinKB or more. When this returns true it has recorded
// that the GC compacts for the merge (the report clears that if the GC did not compact after all). Not while a background GC is in progress, which
// keeps every GC from merging.
bool gc_heap::string_dedup_compaction_pays()
{
    int64_t min_kb = GCConfig::GetStringDedupCompactMinKB();
    string_dedup_queue_state* queue = string_dedup_queue;
    if ((min_kb <= 0) || !string_dedup_merge_enabled() || (queue == nullptr) || (queue->pairs == nullptr) || !queue->gc.report_pending)
    {
        return false;
    }

    string_dedup_merge_stats* stats = &queue->merge;
    if ((settings.condemned_generation != 1) || (stats->status != 0) || ((int64_t)(stats->pair_bytes >> 10) < min_kb))
    {
        return false;
    }

#ifdef BACKGROUND_GC
    if (background_running_p())
    {
        return false;
    }
#endif //BACKGROUND_GC

    stats->upgraded = 1;

    return true;
}

#endif //USE_REGIONS && !MULTIPLE_HEAPS

// Runs after the plan phase of every non-concurrent GC.
void gc_heap::string_dedup_queue_report()
{
    string_dedup_queue_state* queue = string_dedup_queue;
    if (queue == nullptr)
    {
        return;
    }

    // The GC leaves the strings that are in or next to a pinned plug where they are, so a string can end up in a younger generation than
    // it was in. Gen1 and gen0 GCs do not look at entries of gen2, so such entries cannot be kept. A gen2 GC can do this to any entry
    // and a gen1 GC to the ones it queued. Without regions a GC of any generation can demote a whole segment when the heap expands.
    bool queued = queue->gc.report_pending;
    bool check_all = (settings.condemned_generation == max_generation);
#ifndef USE_REGIONS
    check_all |= settings.demotion;
#endif //!USE_REGIONS
    if (!queued && !check_all)
    {
        return;
    }

    string_dedup_queue_stats* stats = &queue->gc;
    if (!queued)
    {
        *stats = {};
        stats->gc_index = (uint64_t)settings.gc_index;
        stats->condemned_generation = (uint64_t)settings.condemned_generation;
        stats->heap_index = (uint64_t)heap_number;
#ifndef MULTIPLE_HEAPS
        queue->find = {};
        queue->find.gc_index = stats->gc_index;
        queue->find.condemned_generation = stats->condemned_generation;
#endif //!MULTIPLE_HEAPS
    }

    stats->report_pending = false;

    uint64_t start_time = GetHighPrecisionTimeStamp();
    size_t first = check_all ? 0 : queue->first_new;
    size_t kept = first;
    for (size_t i = first; i < queue->count; i++)
    {
        if (string_dedup_test::in_gen2 (queue->entries[i].obj))
        {
            queue->entries[kept++] = queue->entries[i];
        }
    }

    stats->demoted = queue->count - kept;
    queue->count = kept;
    stats->elapsed_us += GetHighPrecisionTimeStamp() - start_time;

#ifndef MULTIPLE_HEAPS
    if (string_dedup_find_enabled())
    {
        string_dedup_find_report (queue, queued, check_all);
    }

#ifdef USE_REGIONS
    if (queued && string_dedup_merge_enabled())
    {
        string_dedup_merge_stats* merge = &queue->merge;
        merge->compacted = settings.compaction ? 1 : 0;
        // A veto that came after string_dedup_compaction_pays asked for a compaction leaves a GC that did not compact.
        merge->upgraded &= merge->compacted;
        if (!settings.compaction && (merge->status == 0))
        {
#ifdef BACKGROUND_GC
            // A background GC in progress is the reason that a GC does not merge, whether it compacts (string_dedup_merge_begin) or sweeps.
            merge->status = background_running_p() ? 2 : 1;
#else //BACKGROUND_GC
            merge->status = 1;
#endif //BACKGROUND_GC
        }

        string_dedup_merge_fire (merge);
    }

    queue->pair_count = 0;
#endif //USE_REGIONS
#endif //!MULTIPLE_HEAPS

    if (queued || (stats->demoted != 0))
    {
        string_dedup_queue_fire (stats, queue->count);
    }
}

#ifdef BACKGROUND_GC
// Runs in the final mark of a background GC, when the mark array tells which strings are alive. The sweep that follows frees the other
// strings of gen2 without passing through the other hooks, so their entries are removed here. Every heap does this before the heaps join
// at the end of the final mark, so no heap can sweep before all entries are checked. The EE is suspended, which keeps blocking GCs from
// using the queue at the same time.
void gc_heap::string_dedup_queue_background_mark()
{
    string_dedup_queue_state* queue = string_dedup_queue;
    if (queue == nullptr)
    {
        return;
    }

    assert (settings.concurrent);
    string_dedup_queue_stats stats = {};
    stats.gc_index = (uint64_t)settings.gc_index;
    stats.condemned_generation = (uint64_t)max_generation;
    stats.heap_index = (uint64_t)heap_number;
    stats.background = 1;

    uint64_t start_time = GetHighPrecisionTimeStamp();
    stats.died = string_dedup_queue_remove_if (queue, string_dedup_check_bgc_marked);
    stats.elapsed_us = GetHighPrecisionTimeStamp() - start_time;

    string_dedup_queue_fire (&stats, queue->count);

#ifndef MULTIPLE_HEAPS
    if (queue->table != nullptr)
    {
        string_dedup_find_stats find = {};
        find.gc_index = stats.gc_index;
        find.condemned_generation = stats.condemned_generation;
        find.background = 1;

        start_time = GetHighPrecisionTimeStamp();
        find.died = string_dedup_table_remove_if (queue->table, string_dedup_check_bgc_marked, 0);
        string_dedup_table_shrink (queue->table);
        find.elapsed_us = GetHighPrecisionTimeStamp() - start_time;

        string_dedup_find_fire (&find, queue->table->live);
    }
#endif //!MULTIPLE_HEAPS
}
#endif //BACKGROUND_GC

#ifdef DYNAMIC_HEAP_COUNT
// Moves the entries of a heap that goes out of service to this heap. The entries that do not fit are dropped, they are only candidates.
void gc_heap::string_dedup_queue_merge (gc_heap* from_hp)
{
    string_dedup_queue_state* from = from_hp->string_dedup_queue;
    if ((from == nullptr) || (from->count == 0))
    {
        return;
    }

    if (string_dedup_queue == nullptr)
    {
        string_dedup_queue = new (nothrow) string_dedup_queue_state();
    }

    string_dedup_queue_state* to = string_dedup_queue;
    if ((to != nullptr) && string_dedup_queue_reserve (to))
    {
        size_t room = string_dedup_queue_capacity - to->count;
        size_t moved = (from->count < room) ? from->count : room;
        memcpy (&to->entries[to->count], from->entries, moved * sizeof (string_dedup_candidate));
        to->count += moved;
    }

    from->count = 0;
}
#endif //DYNAMIC_HEAP_COUNT

#ifdef VERIFY_HEAP
// The strings that GC stress allocates are chopped in place, which changes their length and content.
static bool string_dedup_verify_content_p()
{
#if defined(STRESS_HEAP) && !defined(FEATURE_NATIVEAOT)
    return !GCStress<cfg_any>::IsEnabled();
#else
    return true;
#endif //STRESS_HEAP && !FEATURE_NATIVEAOT
}

#ifndef MULTIPLE_HEAPS
// Every string of the table must be a string of gen2 that still has the content it was added for, and the index must have exactly one slot for
// every string that was added since the table was rebuilt, the removed ones included.
static void string_dedup_table_verify (string_dedup_table* table)
{
    bool verify_content_p = string_dedup_verify_content_p();
    if ((table->count > table->capacity) || (table->capacity > table->max_capacity) || (table->live > table->count) ||
        ((table->slot_count & (table->slot_count - 1)) != 0) || (table->slot_count > table->max_slot_count) ||
        ((table->capacity * 4) > (table->slot_count * 3)))
    {
        FATAL_GC_ERROR();
    }

    size_t live = 0;
    for (size_t i = 0; i < table->count; i++)
    {
        uint8_t* o = table->strings[i];
        if (o == nullptr)
        {
            continue;
        }

        live++;
        if ((o < g_gc_lowest_address) || (o >= g_gc_highest_address) || (((size_t)o & (sizeof (void*) - 1)) != 0))
        {
            FATAL_GC_ERROR();
        }

        CObjectHeader* str = (CObjectHeader*)o;
        if (!string_dedup_is_string (str->GetMethodTable()) || (g_theGCHeap->WhichGeneration ((Object*)o) != max_generation))
        {
            FATAL_GC_ERROR();
        }

        size_t free_slot = 0;
        if (verify_content_p)
        {
            string_dedup_slot* slot = string_dedup_table_find (table, table->hashes[i], str, str->GetNumComponents(), &free_slot);
            if ((table->hashes[i] != string_dedup_table_hash (table, str)) || (slot == nullptr) || (slot->entry != (i + 1)))
            {
                FATAL_GC_ERROR();
            }
        }
    }

    size_t used_slots = 0;
    size_t live_slots = 0;
    for (size_t i = 0; i < table->slot_count; i++)
    {
        uint32_t entry = table->slots[i].entry;
        if (entry == 0)
        {
            continue;
        }

        used_slots++;
        if ((entry > table->count) || (table->slots[i].hash != table->hashes[entry - 1]))
        {
            FATAL_GC_ERROR();
        }

        if (table->strings[entry - 1] != nullptr)
        {
            live_slots++;
        }
    }

    if ((live != table->live) || (used_slots != table->count) || (live_slots != live))
    {
        FATAL_GC_ERROR();
    }
}
#endif //!MULTIPLE_HEAPS

// Every entry must still point at the string it was queued for, which fails when an entry was not relocated or kept after its string died,
// and the string must be in gen2, which fails when an entry was not removed after the GC demoted its string.
void gc_heap::string_dedup_queue_verify()
{
    string_dedup_queue_state* queue = string_dedup_queue;
    if (queue == nullptr)
    {
        return;
    }

    bool verify_content_p = string_dedup_verify_content_p();
    for (size_t i = 0; i < queue->count; i++)
    {
        uint8_t* o = queue->entries[i].obj;
        if ((o < g_gc_lowest_address) || (o >= g_gc_highest_address) || (((size_t)o & (sizeof (void*) - 1)) != 0))
        {
            FATAL_GC_ERROR();
        }

        CObjectHeader* str = (CObjectHeader*)o;
        if (!string_dedup_is_string (str->GetMethodTable()) || (verify_content_p && (string_dedup_signature (str) != queue->entries[i].signature)))
        {
            FATAL_GC_ERROR();
        }

        if (g_theGCHeap->WhichGeneration ((Object*)o) != max_generation)
        {
            FATAL_GC_ERROR();
        }
    }

#ifndef MULTIPLE_HEAPS
    if (queue->table != nullptr)
    {
        string_dedup_table_verify (queue->table);
    }

#ifdef USE_REGIONS
    // The merge registers duplicates only while the relocate phase runs, and the strings that the roots note are consumed right after marking.
    if ((string_dedup_redirect_map != nullptr) || (string_dedup_rooted.count != 0))
    {
        FATAL_GC_ERROR();
    }
#endif //USE_REGIONS
#endif //!MULTIPLE_HEAPS
}
#endif //VERIFY_HEAP

#endif //FEATURE_EVENT_TRACE

} // namespace WKS/SVR
