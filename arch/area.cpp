#include <arch/MGSystem.h>

#ifdef ENABLE_CACTI
#include <arch/mem/coma/COMA.h>

#include <sim/log2.h>
#include <cacti/cacti_interface.h>

// These values are taken from Gupta, et al.'s paper:
// "Technology Independent Area and Delay Estimates for Microprocessor Building Blocks"
// The values are expressed in lambda-squared, where lambda is half the technology size.
static const double ALU_Area_Per_Bit  = 2.41e6;
static const double Mult_Area_Per_Bit = 1.84e6;
static const double FPU_Area_Per_Bit  = 4.55e6;

struct config
{
    double       tech;      // in nanometers

    unsigned int bits_PSize;
    unsigned int bits_FCapability;
    unsigned int bits_TSize;
    unsigned int bits_RegIndex;
    unsigned int bits_RegAddr;
    unsigned int bits_RegValue;
    unsigned int bits_RegsNo;
    unsigned int bits_SInteger;
    unsigned int bits_MemAddr;
    unsigned int bits_bool;
    unsigned int bits_CID;
    unsigned int bits_unsigned;
    unsigned int bits_LFID;
    unsigned int bits_TID;
    unsigned int bits_PID;
    unsigned int bits_BlockSize;
    
    unsigned int numProcessors;
    unsigned int numFPUs;
    unsigned int numThreads;
    unsigned int numFamilies;
    unsigned int numIntRegisters;
    unsigned int numFltRegisters;
};

struct org_t
{
    double area;        // in um^2
    double access_time; // in s
    double cycle_time;  // in s
    
    void merge(const org_t& o) {
        area += o.area;
        access_time = std::max(access_time, o.access_time);
        cycle_time = std::max(cycle_time, o.cycle_time);
    }
    
    org_t(double area_, double access_time_, double cycle_time_)
        : area(area_), access_time(access_time_), cycle_time(cycle_time_)
    {
    }
};

struct cache_desc
{
    unsigned int sets;
    unsigned int assoc;
    unsigned int tag;
    unsigned int linesize;
    unsigned int r_ports;
    unsigned int w_ports;
    unsigned int rw_ports;
};

struct field_desc {
    const char* name;
    const unsigned int config::*bits;
    unsigned int mult;
    unsigned int r_ports;
    unsigned int w_ports;
    unsigned int rw_ports;
};

struct structure_desc {
    const char* name;
    const unsigned int config::*rows;
    field_desc fields[40];
};

struct tcache_desc {
    const char* name;
    cache_desc  desc;
    size_t      count;
};

#define FIELD(type, name, rp, wp, rwp) \
    {#name, &config::bits_##type, 1, rp, wp, rwp}

#define FIELD_2(type, name, rp, wp, rwp) \
    {#name, &config::bits_##type, 2, rp, wp, rwp}

static const structure_desc int_register_file = {
    "int_register_file",
    &config::numIntRegisters,
    {
    FIELD(RegValue, Value, 2, 0, 2),
    {0,0,0,0,0,0}
    }
};

static const structure_desc flt_register_file = {
    "flt_register_file",
    &config::numFltRegisters,
    {
    FIELD(RegValue, Value, 2, 0, 2),
    {0,0,0,0,0,0}
    }
};

static const structure_desc family_table = {
    "family_table",
    &config::numFamilies,
    {
    FIELD(PSize, placeSize, 4, 1, 0),       // W: allocate process, writing from allocate message
                                            // R: network allocate response process (to determine first core)
                                            // R: fetch stage (pass to execute stage; for default create place size)
                                            // R: local create process, distribution calculation
                                            // R: remote create process, distribution calculation
                                            
    FIELD(PSize, numCores, 1, 2, 1),        // W: network allocate process
                                            // W: network allocate response process
                                            // R: remote create process, distribution calculation
                                            // RW: local create process, distribution calculation
                                            
    FIELD(FCapability, capability, 2, 1, 0),   // R: create process (create completion; compose FID)
                                               // R: network handler (capability check for incoming messages)
                                               // W: local family allocation process
                                                
    FIELD(MemAddr, pc, 2, 2, 0),                // R: thread allocation process
                                                // R: create process
                                                // W: remote create handler
                                                // W: local create handler
    
    FIELD(TSize, physBlockSize, 1, 2, 2),      // W: local family allocation process (set to default)
                                                // W: network's delegate message handler (set property)
                                                // W: network's link message handler (set property)
                                                // RW: family creation process (register allocation; update) to final
                                                // RW: link process (remote family creation; register allocation; update to final)
                                                // R: thread allocation process (check if block size reached)
    
    FIELD(SInteger, start, 2, 3, 1),            // W: local family allocation process (set to default)
                                                // W: network's delegate message handler (set property)
                                                // W: network's link message handler (set property)
                                                // R: local family create process (thread distribution)
                                                // R: remote family create process (thread distribution)
                                                // RW: thread allocation process (calculation thread index, and update for next)
    
    FIELD(SInteger, step, 3, 3, 0),             // W: local family allocation process (set to default)
                                                // W: network's delegate message handler (set property)
                                                // W: network's link message handler (set property)
                                                // R: thread allocation process (update thread index for next)
                                                // R: local family create process (thread distribution)
                                                // R: remote family create process (thread distribution)

    FIELD(SInteger, limit, 0, 3, 3),            // W: local family allocation process (set to default)
                                                // W: network's delegate message handler (set property)
                                                // W: network's link message handler (set property)
                                                // RW: local family create process (thread distribution)
                                                // RW: remote family create process (thread distribution)
                                                // *** as nThreads: ***
                                                // RW: thread allocation process (update thread count)
                                                
                                                                                                    
    FIELD(bool, hasShareds, 1, 1, 0),           // R: thread allocation process
                                                // W: local create process (set based on loaded register count)
    
    FIELD(bool, allocationDone, 2, 1, 3),       // RW: network's remote handler (break, detach)
                                                // RW: network's link handler (break, detach, done)
                                                // W: local family allocation process (set to default)
                                                // RW: thread allocation process
                                                // R: DCache's read handler (dependency check)
                                                // R: Network's sync handler (dependency check)
                                                
    FIELD(bool, prevSynched, 4, 1, 1),          // W: local family allocation (set to initial value)
                                                // R: network's remote handler (break, detach)
                                                // RW: network's link handler (break, detach, done)
                                                // R: thread allocation process (dependency check)
                                                // R: DCache's read handler (dependency check)
                                                // R: Network's sync handler (dependency check)
        
    FIELD(bool, detached, 3, 1, 2),             // W: local family allocation (set to initial value)
                                                // RW: network's remote handler (break, detach)
                                                // RW: network's link handler (break, detach, done)
                                                // R: thread allocation process (dependency check)
                                                // R: DCache's read handler (dependency check)
                                                // R: Network's sync handler (dependency check)

    FIELD(bool, syncSent, 2, 2, 2),             // W: local family allocation (set to initial value)
                                                // RW: network's remote handler (break, detach, sync)
                                                // RW: network's link handler (break, detach, done, sync)
                                                // R: thread allocation process (dependency check)
                                                // R: DCache's read handler (dependency check)
                                                // W: Network's sync handler

    FIELD(TSize, numThreadsAllocated, 2, 1, 3), // W: local family allocation process (set to default)
                                                    // RW: thread allocation process
                                                    // RW: network's remote handler (break, detach)
                                                    // RW: network's link handler (break, detach, done)
                                                    // R: DCache's read handler (dependency check)
                                                    // R: Network's sync handler (dependency check)

    FIELD(unsigned, numPendingReads, 4, 1, 2),      // W: local family allocation process (set to default)
                                                    // RW: pipeline memory stage (increment)
                                                    // R: thread allocation process (dependency check)
                                                    // R: network's remote handler (break, detach)
                                                    // R: network's link handler (break, detach, done)
                                                    // RW: DCache's read handler (decrement)
                                                    // R: Network's sync handler (dependency check)
                                                                                  
    FIELD(LFID, link, 4, 1, 3),                     // W: local family allocation process (set to prev FID)
                                                    // RW: network's link handler (create forward, with possible restrict)
                                                    // RW: local create process (create forward, with possible restrict)
                                                    // RW: network's allocation response (forward to prev; set to next)
                                                    // R: thread allocation process (dependency check)
                                                    // R: network's remote handler (forward message/send done)
                                                    // R: network's link handler (forward message/send done)
                                                    // R: DCache's read handler (dependency check)
    
    FIELD(bool, prevCleanedUp, 0, 1, 1),            // W: local family allocation process (set to initial)
                                                    // RW: thread allocation process (dependency initialization)
    
    FIELD(RegIndex, sync_reg, 2, 2, 1),             // R: thread allocation process (dependency check)
                                                    // R: DCache's read handler (dependency check)
                                                    // W: local family allocation process (set to initial)
                                                    // W: network's remote handler (sync)
                                                    // RW: network's link handler (sync, done)

    FIELD(PID, sync_pid, 2, 2, 1),                  // R: thread allocation process (dependency check)
                                                    // R: DCache's read handler (dependency check)
                                                    // W: local family allocation process (set to initial)
                                                    // W: network's remote handler (sync)
                                                    // RW: network's link handler (sync, done)

    FIELD(bool, sync_done, 1, 3, 1),                // W: thread allocation process (dependency check)
                                                    // W: DCache's read handler (dependency check)
                                                    // W: local family allocation process (set to initial)
                                                    // R: network's remote handler (sync)
                                                    // RW: network's link handler (sync, done)
    
    FIELD(bool, lastAllocated, 0, 1, 1),            // RW: thread allocation process (setup nextInBlock; update field)
                                                    // W: local family allocation process (set to initial)
                                                    
    FIELD_2(RegsNo, reg_count, 3, 1, 0),            // R: thread allocation process (reg context calculation)
                                                    // W: local create process
                                                    // R: network's remote handler (write reg)
                                                    // R: network's link handler (write reg, create)
    
    FIELD_2(RegIndex, reg_base, 4, 1, 1),           // R: thread allocation process (reg context calculation; free context)
                                                    // W: local create process (set to initial)
                                                    // R: network's remote handler (write reg; free context)
                                                    // RW: network's link handler (write reg; free context; create)
                                                    // R: DCache's read handler (free context)
                                                    // R: Network's sync handler (free context)

    FIELD_2(BlockSize, reg_size, 1, 1, 1),          // W: local create process (set to initial)
                                                    // R: network's remote handler (write reg)
                                                    // RW: network's link handler (write reg; create)

    FIELD_2(RegIndex, reg_last_shareds, 1, 2, 1),   // RW: thread allocation process (setup dependents and overwrite)
                                                    // W: local create handler
                                                    // W: network's link handler (create)
                                                    // R: network's remote handler (write shared)
    
    {0,0,0,0,0,0},
    }
};

static const structure_desc thread_table = {
    "thread_table",
    &config::numThreads,
    {
    FIELD(MemAddr, pc, 2, 2, 0),                    // W: writeback stage (reschedule thread)
                                                    // W: thread allocation process (setup initial)
                                                    // R: thread scheduler (check I-cache)
                                                    // R: fetch stage (thread switch)
                                                    
    FIELD_2(RegIndex, reg_locals, 1, 1, 0),         // W: thread allocation process (setup initial)
                                                    // R: fetch stage (thread switch; for decode stage)

    FIELD_2(RegIndex, reg_dependents, 1, 0, 1),     // RW: thread allocation process (read for clear, use as shareds, setup initial)
                                                    // R: fetch stage (thread switch; for decode stage)
                                                    
    FIELD_2(RegIndex, reg_shareds, 1, 1, 0),        // W: thread allocation process (setup initial)
                                                    // R: fetch stage (thread switch; for decode stage)
                                                    
    FIELD(bool, killed, 1, 1, 1),                   // RW: thread allocation process (setup initial; check dependencies)    
                                                    // W: writeback stage (kill thread)
                                                    // R: DCache write completion process (check dependencies)
                                                    
    FIELD(bool, prev_cleaned_up, 2, 1, 0),          // W: thread allocation process (setup initial; mark cleanup)
                                                    // R: DCache write completion process (check dependencies)
                                                    // R: writeback stage (check dependencies)
                                                    
    FIELD(unsigned, pending_writes, 3, 0, 3),       // RW: thread allocation process (setup initial; check dependencies)
                                                    // R: Execute stage (memory write barrier)
                                                    // R: Writeback stage (memory write barrier)
                                                    // RW: DCache write completion process (decrement)
                                                    // RW: memory stage (increment)
                                                    // R: writeback stage (check dependencies)
                                                    
    FIELD(bool, waiting_writes, 0, 2, 1),           // W: thread allocation process (setup initial)
                                                    // RW: DCache write completion process (check for memory barrier; wakeup)
                                                    // W: writeback stage (suspend) 
                                                    
    FIELD(TID, block_next, 0, 0, 1),                // RW: thread allocation process (setup initial; mark cleanup)

    FIELD(CID, cid, 2, 1, 0),                       // W: thread scheduler (check I-cache)
                                                    // R: fetch stage (read cache-line)
                                                    // R: writeback stage (release cache-line)
                                                    
    FIELD(LFID, family, 1, 0, 1),                   // RW: thread allocation process (setup initial; cleanup)
                                                    // R: fetch stage (thread switch)
                                                    
    FIELD(TID, next, 1, 1, 1),                      // RW: thread scheduler (queue activate threads; pop ready thread), thread allocation process (reschedule after creation), D-Cache (reschedule after memory barrier), register file writes (wakeup)
                                                    // W: writeback stage (reschedule after switch / suspend on register)
                                                    // R: fetch stage (pop active thread)
    {0,0,0,0,0,0}
    }
};

static org_t get_info(
    int cache_size,
    int line_size,
    int associativity,
    int rw_ports,
    int excl_read_ports,
    int excl_write_ports,
    double tech_node,
    int output_width,
    int tag_width,
    int cache)
{
    uca_org_t o = cacti_interface(
        cache_size,
        line_size,
        associativity,
        rw_ports,
        excl_read_ports,
        excl_write_ports,
        0, /* single_ended_read_ports */
        1, /* banks */
        tech_node,
        8192, /* page_sz */
        8, /* burst_length */
        8, /* pre_Width */
        output_width,
        tag_width != 0, /* specific_tag */
        tag_width,
        1, /* access_mode: 0 normal, 1 seq, 2 fast */
        cache, /* cache: 0 - scratch ram, 1 - cache */
        0, /* main_mem */
        0,   /* obj_func_delay */
        0,   /* obj_func_dynamic_power */
        0,   /* obj_func_leakage_power */
        100, /* obj_func_area */
        0,   /* obj_func_cycle_time */
        60,      /* dev_func_delay */
        100000,  /* dev_func_dynamic_power */
        100000,  /* dev_func_leakage_power */
        1000000, /* dev_func_area */
        100000,  /* dev_func_cycle_time */
        0, /* Optimize: 0 - None (use weight and deviate), 1 - ED, 2 - ED^2 */
        350, /* temp */
        3, /* wt: 0 - default(search across everything), 1 - global, 2 - 5% delay penalty, 3 - 10%, 4 - 20 %, 5 - 30%, 6 - low-swing */
        itrs_hp, /* data_arr_ram_cell_tech_flavor_in */
        itrs_hp, /* data_arr_peri_global_tech_flavor_in */
        itrs_hp, /* tag_arr_ram_cell_tech_flavor_in */
        itrs_hp, /* tag_arr_peri_global_tech_flavor_in */
        1, /* interconnect_projection_type_in: 0 - aggressive, 1 - conservative */
        2, /* wire_inside_mat_type_in (0 - local, 2 - global) */
        2, /* wire_outside_mat_type_in (0 - local, 2 - global) */
        0, /* is_nuca: 0 - UCA, 1 - NUCA */
        8, /* core_count */
        0, /* cache_level: 0 - L2, 1 - L3 */
        0, /* nuca_bank_count */
        100, /* nuca_obj_func_delay */
        100, /* nuca_obj_func_dynamic_power */
        0,   /* nuca_obj_func_leakage_power */
        100, /* nuca_obj_func_area */
        0,   /* nuca_obj_func_cycle_time */
        10,    /* nuca_dev_func_delay */
        10000, /* nuca_dev_func_dynamic_power */
        10000, /* nuca_dev_func_leakage_power */
        10000, /* nuca_dev_func_area */
        10000, /* nuca_dev_func_cycle_time */
        1, /* REPEATERS_IN_HTREE_SEGMENTS_in: TODO for now only wires with repeaters are supported */
        0 /*p_input*/);

    return org_t(o.cache_ht * o.cache_len, o.access_time, o.cycle_time);
}

static org_t get_ram_info(int rows, int width, int rw_ports, int r_ports, int w_ports, double tech)
{
    return get_info(rows * width, width, 1, rw_ports, r_ports, w_ports, tech, width, 0, 0);
}

static org_t get_cache_info(const cache_desc& desc, double tech)
{
    unsigned int actual_sets = desc.sets;
    unsigned int cacti_sets  = std::max(actual_sets, 16u);
    org_t i = get_info(cacti_sets * desc.assoc * desc.linesize, desc.linesize, desc.assoc, desc.rw_ports, desc.r_ports, desc.w_ports, tech, desc.linesize * 8, desc.tag, 1);
    i.area = i.area * actual_sets / cacti_sets;
    return i;
}

static org_t get_structure_info(std::ostream& os, const config& config, const structure_desc& desc)
{
    org_t info(0,0,0);
    for (const field_desc* fld = desc.fields; fld->name != 0; ++fld)
    {
        unsigned int actual_bits = config.*fld->bits * fld->mult;
        unsigned int cacti_bits  = (actual_bits + 7) / 8 * 8;
        
        // Expand rows until cache size is at least 64 bytes
        unsigned int actual_rows = config.*desc.rows;
        unsigned int cacti_rows  = std::max(actual_rows, (512 + cacti_bits - 1) / cacti_bits);

        org_t i = get_ram_info(cacti_rows, cacti_bits / 8, fld->r_ports, fld->w_ports, fld->rw_ports, config.tech);
        
        i.area = i.area * actual_bits / cacti_bits;
        i.area = i.area * actual_rows / cacti_rows;
        
        os << desc.name << "\t" << fld->name << "\t" << i.area*1e-6 << "\t" << i.access_time*1e9 << std::endl;
        
        info.merge(i);
    }
    return info;
}

static void DumpStatsCOMA(std::ostream& os, const Simulator::COMA& coma)
{
    unsigned int numCaches      = coma.GetNumCaches();
    unsigned int numDirectories = coma.GetNumDirectories();
    unsigned int numRootDirs    = coma.GetNumRootDirectories();
    
    os << "L2 caches: " << numCaches << std::endl
       << "Directories: " << numDirectories << std::endl
       << "Root Directories: " << numRootDirs << std::endl;
}

static org_t DumpAreaCOMA(std::ostream& os, const config& config, const Simulator::COMA& coma)
{
    unsigned int lineSize       = coma.GetLineSize();
    unsigned int numSets        = coma.GetNumCacheSets();
    unsigned int assoc          = coma.GetCacheAssociativity();
    unsigned int numCaches      = coma.GetNumCaches();
    unsigned int numDirectories = coma.GetNumDirectories();
    unsigned int numRootDirs    = coma.GetNumRootDirectories();
    unsigned int bits_tag       = config.bits_MemAddr - ilog2(lineSize) - ilog2(numSets);
    unsigned int bits_numCaches = ilog2(numCaches);

    unsigned int numCachesPerLowRing = coma.GetNumCachesPerLowRing();
    
    static const tcache_desc l2_cache = {
        "l2_cache",
        {numSets, assoc,
        bits_tag + 2 + ilog2(assoc) + bits_numCaches + 1 + 9,
        lineSize + lineSize / 8, /* data + dirty bitmask */
        0, 0, 1    /* one port; access is arbitrated */
        },
        numCaches,
    };

    static const tcache_desc directory = {
        "directory",
        {numSets, assoc * numCachesPerLowRing,
        bits_tag + 1 + bits_numCaches,
        1, /* dummy data for CACTI */
        0, 0, 1    /* one port; access is arbitrated */
        },
        numDirectories,
    };

    static const tcache_desc root_directory = {
        "root_directory",
        {numSets, assoc * numCaches,
        bits_tag - ilog2(numRootDirs) + 2 + bits_numCaches + bits_numCaches,
        1, /* dummy data for CACTI */
        0, 0, 1    /* one port; access is arbitrated */
        },
        numRootDirs,
    };

    static const tcache_desc* caches[] = {
        &l2_cache,
        &directory,
        &root_directory,
    };
    
    org_t total(0,0,0);
    for (size_t i = 0; i < sizeof caches / sizeof caches[0]; ++i)
    {
        const tcache_desc& c = *caches[i];
        org_t info = get_cache_info(c.desc, config.tech);
        os << c.name << "\t\t" << info.area*1e-6 << "\t" << info.access_time*1e9 << std::endl;
        info.area *= c.count;
        total.merge(info);
    }
    return total;
}
    
void Simulator::MGSystem::DumpArea(std::ostream& os, unsigned int tech) const
{
    // Virtual register size (registers in ISA)
    static const unsigned int BITS_VREG = 5;
    
    config cfg;
    
    cfg.numProcessors   = m_procs[0]->GetGridSize();
    cfg.numFPUs         = m_fpus.size();
    cfg.numThreads      = m_procs[0]->GetThreadTableSize();
    cfg.numFamilies     = m_procs[0]->GetFamilyTableSize();
    cfg.numIntRegisters = m_procs[0]->GetRegisterFile().GetSize(RT_INTEGER);
    cfg.numFltRegisters = m_procs[0]->GetRegisterFile().GetSize(RT_FLOAT);
    
    cfg.tech             = tech;
    cfg.bits_PID         =
    cfg.bits_PSize       = ilog2(cfg.numProcessors);
    cfg.bits_TID         = 
    cfg.bits_TSize       = ilog2(cfg.numThreads);
    cfg.bits_RegsNo      = NUM_REG_TYPES * BITS_VREG;
    cfg.bits_RegIndex    = ilog2(std::max(cfg.numIntRegisters, cfg.numFltRegisters));
    cfg.bits_RegAddr     = cfg.bits_RegIndex + ilog2((int)NUM_REG_TYPES);
    cfg.bits_RegValue    = 
    cfg.bits_SInteger    = sizeof(SInteger) * 8;
    cfg.bits_LFID        = ilog2(cfg.numFamilies);
    
    if (cfg.bits_PID + cfg.bits_LFID >= cfg.bits_RegValue)
    {
        std::cerr << "Error: No space in registers for capability bits" << std::endl;
        return;
    }

    cfg.bits_FCapability = cfg.bits_RegValue - cfg.bits_PID - cfg.bits_LFID;
    cfg.bits_MemAddr     = sizeof(MemAddr) * 8;
    cfg.bits_bool        = 1;
    cfg.bits_CID         = ilog2(m_procs[0]->GetICache().GetNumLines());
    cfg.bits_unsigned    = BITS_VREG; /* # outstanding reads */
    cfg.bits_BlockSize   = BITS_VREG; /* from RAUnit.cpp */

    // We only dump information for the memory if it is COMA
    Simulator::COMA* coma = dynamic_cast<Simulator::COMA*>(m_memory);
    
    // Dump processor structures
    static const structure_desc* structures[] = {
        &int_register_file,
        &flt_register_file,
        &family_table,
        &thread_table,
    };

    os << "Technology size: " << tech << " nm" << std::endl
       << "Processors: " << cfg.numProcessors << std::endl
       << "FPUs: " << cfg.numFPUs << std::endl;
    if (coma != NULL)
    {
        DumpStatsCOMA(os, *coma);
    }
    
    os << std::endl
       << "Structure\tField\tArea (mm^2)\tAccess time (ns)" << std::endl;

    org_t coreinfo(0,0,0);
    for (size_t i = 0; i < sizeof structures / sizeof structures[0]; ++i)
    {
        const structure_desc& s = *structures[i];
        org_t si = get_structure_info(os, cfg, s);
        std::cout << s.name << "\t(total,max)\t" << si.area*1e-6 << " " << si.access_time*1e9 << std::endl;
        coreinfo.merge(si);
    }

    // Dump processor caches
    {
        const Simulator::Processor::ICache& icache = m_procs[0]->GetICache();
        const Simulator::Processor::DCache& dcache = m_procs[0]->GetDCache();
        
        static const tcache_desc l1_icache = {
            "l1_icache",
            {icache.GetNumSets(), icache.GetAssociativity(),
            cfg.bits_MemAddr - ilog2(icache.GetLineSize()) - ilog2(icache.GetNumSets()) + /*extra*/ 2 + ilog2(icache.GetAssociativity()) + 1 + cfg.bits_TID*3,
            icache.GetLineSize(),
            0, 0, 2    /* R/W port for proc->mem and mem->proc */
            },
            1
        };

        static const tcache_desc l1_dcache = {
            "l1_dcache",
            {dcache.GetNumSets(), dcache.GetAssociativity(),
            cfg.bits_MemAddr - ilog2(dcache.GetLineSize()) - ilog2(dcache.GetNumSets()) + /*extra*/ 2 + ilog2(dcache.GetAssociativity()) + 1 + cfg.bits_RegAddr,
            dcache.GetLineSize() + dcache.GetLineSize() / 8, /* data + dirty bitmask */
            0, 0, 2    /* R/W port for proc->mem and mem->proc */
            },
            1
        };

        static const tcache_desc* caches[] = {
            &l1_icache,
            &l1_dcache,
        };
    
        for (size_t i = 0; i < sizeof caches / sizeof caches[0]; ++i)
        {
            const tcache_desc& c = *caches[i];
            org_t si = get_cache_info(c.desc, cfg.tech);
            os << c.name << "\t\t" << si.area*1e-6 << "\t" << si.access_time*1e9 << std::endl;
            coreinfo.merge(si);
        }
    }
    
    // Dump misc. components
    // llb = lambda squared * #bits * adjustment for um^2
    const double llb = tech * tech * cfg.bits_RegValue / 4e6;
    org_t alu(ALU_Area_Per_Bit * llb, 0, 0);
    org_t mult(Mult_Area_Per_Bit * llb, 0, 0);
    org_t fpu(FPU_Area_Per_Bit * llb, 0, 0);

    os << "alu\t\t" << alu.area*1e-6 << "\t" << alu.access_time*1e9 << std::endl
       << "multiplier\t\t" << mult.area*1e-6 << "\t" << mult.access_time*1e9 << std::endl
       << std::endl;

    coreinfo.merge(alu);
    coreinfo.merge(mult);
    
    os << "processor\t(total,max)\t" << coreinfo.area*1e-6 << "\t" << coreinfo.access_time*1e9 << std::endl
       << "fpu\t\t" << fpu.area*1e-6 << "\t" << fpu.access_time*1e9 << std::endl;

    coreinfo.area *= cfg.numProcessors;
    fpu.area *= cfg.numFPUs;

    org_t grid(0,0,0);
    grid.merge(coreinfo);
    grid.merge(fpu);
    
    // Dump memory, if we can
    if (coma != NULL)
    {
        org_t mem = DumpAreaCOMA(os, cfg, *coma);
        grid.merge(mem);
    }
    
    os << "microgrid\t(total,max)\t" << grid.area*1e-6 << "\t" << grid.access_time*1e9 << std::endl;
}

#else
// CACTI not enabled
void Simulator::MGSystem::DumpArea(std::ostream& os, unsigned int tech) const
{
    assert(false);
}
#endif
