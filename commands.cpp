#include <iomanip>
#include <iostream>
#include <sstream>
#include "commands.h"
#include "Processor.h"
#include "SimpleMemory.h"
#include "ParallelMemory.h"
#include "BankedMemory.h"
using namespace std;
using namespace Simulator;

/**
 ** Memory component functions
 **/
static bool cmd_mem_help(Object* obj, const vector<string>& arguments )
{
    if (dynamic_cast<SimpleMemory*>(obj) == NULL) return false;

    cout <<
    "- read <memory-component> <address> <count>\n"
    "Reads and displays 'count' bytes from 'address' from the memory.\n\n"
    "- info <memory-component>\n"
    "Displays statistics for the memory.\n";
    return true;
}

static void cmd_simplemem_requests(SimpleMemory* mem)
{
    cout << " Address  | Size | CPU  | CID  | Type" << endl;
    cout << "----------+------+------+------+------------" << endl;

    typedef queue<SimpleMemory::Request> RequestQueue;
    RequestQueue requests = mem->getRequests();
    while (!requests.empty())
    {
        const RequestQueue::value_type& req = requests.front();
        cout << hex << setfill('0') << right << " "
             << setw(8) << req.address << " | "
             << setw(4) << req.data.size << " | ";

        Processor* cpu = dynamic_cast<Processor*>(req.callback);
        if (cpu == NULL) {
                cout << "???? | ";
        } else {
                cout << cpu->getName() << " | ";
        }

        if (req.data.tag.cid == INVALID_CID) {
            cout << "N/A  | ";
        } else {
            cout << dec << setw(4) << req.data.tag.cid << " | ";
        }

        if (req.write) {
	        cout << "Data write";
        } else if (req.data.tag.data) {
		    cout << "Data read";
        } else if (req.data.tag.cid != INVALID_CID) {
            cout << "Cache-line";
        }
        cout << endl;
        requests.pop();
    }

    cout << endl << "First request done at: ";
    if (mem->getRequests().empty() || mem->getRequests().front().done == 0) {
        cout << "N/A";
    } else {
        cout << dec << setw(8) << mem->getRequests().front().done;
    }
    cout << endl << endl;
}

static void cmd_parallelmem_requests(ParallelMemory* mem)
{
    // Display in-flight requests
    for (size_t i = 0; i < mem->getNumPorts(); i++) {
        cout << "    Done   | Address  | Size | CID  ";
    }
    cout << endl;
    for (size_t i = 0; i < mem->getNumPorts(); i++) {
        cout << "-----------+----------+------+----- ";
    }
    cout << endl;

	vector<multimap<CycleNo, ParallelMemory::Request>::const_iterator> iters;
	for (size_t i = 0; i < mem->getNumPorts(); i++)
	{
		iters.push_back(mem->getPort(i).m_inFlight.begin());
	}

	for (size_t y = 0; y < mem->getConfig().width; y++)
	{
		for (size_t x = 0; x < mem->getNumPorts(); x++)
		{
			multimap<CycleNo, ParallelMemory::Request>::const_iterator& p = iters[x];

			const ParallelMemory::Port& port = mem->getPort(x);
			if (p != port.m_inFlight.end())
			{
				const ParallelMemory::Request& req = p->second;

				if (req.write) {
					cout << "W";
				} else if (req.data.tag.data) {
					cout << "R";
				} else if (req.data.tag.cid != INVALID_CID) {
					cout << "C";
				}

				cout << setw(9) << dec << p->first << " | "
					 << setw(8) << hex << right << req.address << " | "
				     << setw(4) << req.data.size << " | ";

				if (req.write || req.data.tag.cid == INVALID_CID) {
					cout << "N/A ";
				} else {
					cout << dec << setw(4) << req.data.tag.cid;
				}
				p++;
			}
			else
			{
				cout << "           |          |      |     ";
			}
			cout << " ";
		}
		cout << endl;
	}
    cout << endl << endl;
}

static void print_bankedmem_pipelines(const vector<BankedMemory::Pipeline>& queues)
{
    for (size_t i = 0; i < queues.size(); i++) {
        cout << "    Done   | Address  | Size | CID  ";
    }
    cout << endl;
    for (size_t i = 0; i < queues.size(); i++) {
        cout << "-----------+----------+------+----- ";
    }
    cout << endl;
    
	vector<BankedMemory::Pipeline::const_iterator> iters;
	size_t length = 0;
	for (size_t i = 0; i < queues.size(); i++)
	{
		iters.push_back(queues[i].begin());
		length = max(length, queues[i].size());
	}

	for (size_t y = 0; y < length; y++)
	{
		for (size_t x = 0; x < queues.size(); x++)
		{
			BankedMemory::Pipeline::const_iterator& p = iters[x];

			if (p != queues[x].end())
			{
				const BankedMemory::Request& req = p->second;

				if (req.write) {
					cout << "W";
				} else if (req.data.tag.data) {
					cout << "R";
				} else if (req.data.tag.cid != INVALID_CID) {
					cout << "C";
				}

				cout << setw(9) << dec << p->first << " | "
					 << setw(8) << hex << right << req.address << " | "
				     << setw(4) << req.data.size << " | ";

				if (req.write || req.data.tag.cid == INVALID_CID) {
					cout << "N/A ";
				} else {
					cout << dec << setw(4) << req.data.tag.cid;
				}
				p++;
			}
			else
			{
				cout << "           |          |      |     ";
			}
			cout << " ";
		}
		cout << endl;
	}
    cout << endl << endl;
}

static void cmd_bankedmem_requests(BankedMemory* mem)
{
    print_bankedmem_pipelines( mem->GetIncoming() );
    
    // Print the banks
    const vector<BankedMemory::Bank>& banks = mem->GetBanks();
    for (size_t i = 0; i < banks.size(); i++) {
        cout << "    Done   | Address  | Size | CID  ";
    }
    cout << endl;
    for (size_t i = 0; i < banks.size(); i++) {
        cout << "-----------+----------+------+----- ";
    }
    cout << endl;
    for (size_t i = 0; i < banks.size(); i++)
	{
		if (banks[i].busy)
		{
			const BankedMemory::Request& req = banks[i].request;

			if (req.write) {
				cout << "W";
			} else if (req.data.tag.data) {
				cout << "R";
			} else if (req.data.tag.cid != INVALID_CID) {
				cout << "C";
			}

			cout << setw(9) << dec << banks[i].done << " | "
				 << setw(8) << hex << right << req.address << " | "
			     << setw(4) << req.data.size << " | ";

			if (req.write || req.data.tag.cid == INVALID_CID) {
				cout << "N/A ";
			} else {
				cout << dec << setw(4) << req.data.tag.cid;
			}
		}
		else
		{
			cout << "           |          |      |     ";
		}
		cout << " ";
	}
    cout << endl << endl;    
    
    print_bankedmem_pipelines( mem->GetOutgoing() );
}


// Reads a number of bytes from memory and displays them
// Usage: read <mem-component> <address> <count>
static bool cmd_mem_read(Object* obj, const vector<string>& arguments )
{
    IMemoryAdmin* mem = dynamic_cast<IMemoryAdmin*>(obj);
    if (mem == NULL) return false;

    MemAddr addr = 0;
    MemSize size = 0;
    char* endptr;

    // Check input
    if (arguments.size() == 1 && arguments[0] == "requests")
    {
        if (dynamic_cast<SimpleMemory*>(mem) != NULL)
        {
			cmd_simplemem_requests( dynamic_cast<SimpleMemory*>(mem) );
        }
        else if (dynamic_cast<ParallelMemory*>(mem) != NULL)
        {
            cmd_parallelmem_requests( dynamic_cast<ParallelMemory*>(mem) );
        }
        else if (dynamic_cast<BankedMemory*>(mem) != NULL)
        {
            cmd_bankedmem_requests( dynamic_cast<BankedMemory*>(mem) );
        }
        return true;
    }

    if (arguments.size() == 2)
    {
        addr = strtoul( arguments[0].c_str(), &endptr, 0 );
        if (*endptr == '\0')
        {
            size = strtoul( arguments[1].c_str(), &endptr, 0 );
        }
    }

    if (arguments.size() != 2 || *endptr != '\0')
    {
        cout << "Usage: read <mem> <address> <count>" << endl;
        return true;
    }

    // Calculate aligned start and end addresses
    MemAddr start = addr & -16;
    MemAddr end   = (addr + size + 15) & -16;

    uint8_t* buf = NULL;
    try {
        // Read the data
        buf = new uint8_t[(size_t)size];
        mem->read(addr, buf, size);
    
        // Print it, 16 bytes per row
        for (MemAddr y = start; y < end; y += 16)
        {
            // The address
            cout << setw(8) << hex << setfill('0') << y << " | ";

            // The bytes
            for (MemAddr x = 0; x < 16; x++)
            {
                if (y + x >= addr && y + x < addr + size)
                    cout << hex << setw(2) << setfill('0') << (unsigned int)buf[y + x - addr];
                else
                    cout << "  ";
                if (x == 7) cout << "  ";
                cout << " ";
            }
            cout << "| ";

            // The bytes, as characters
            for (MemAddr x = 0; x < 16; x++)
            {
                unsigned char c = buf[y + x - addr];
                if (y + x >= addr && y + x < addr + size)
                    c = isprint(c) ? c : '.';
                else c = ' ';
                cout << c;
            }

            cout << endl;
        }
    }
    catch (Exception &e)
    {
        cout << "An exception occured while reading the memory:" << endl;
        cout << e.getMessage() << endl;
    }
    delete[] buf;
    return true;
}

// Reads statistics for the memory
// Usage: info <mem-component>
static bool cmd_mem_info(Object* obj, const vector<string>& arguments )
{
    VirtualMemory* mem = dynamic_cast<VirtualMemory*>(obj);
    if (mem == NULL) return false;

	const VirtualMemory::BlockMap& blocks = mem->getBlockMap();
	cout << "Allocated memory blocks:" << endl
		 << "-------------------------" << endl;

	cout << hex << setfill('0');

	MemSize total = 0;
	VirtualMemory::BlockMap::const_iterator p = blocks.begin();
	if (p != blocks.end())
	{
		MemAddr begin = p->first;
		int     perm  = p->second.permissions;
		MemAddr size  = VirtualMemory::BLOCK_SIZE;
		for (++p; p != blocks.end(); )
		{
			MemAddr addr = p->first;
			if (addr > begin + size || p->second.permissions != perm) {
				cout << setw(8) << begin << " - " << setw(8) << begin + size - 1 << ": ";
				cout << (perm & IMemory::PERM_READ    ? "R" : ".");
				cout << (perm & IMemory::PERM_WRITE   ? "W" : ".");
				cout << (perm & IMemory::PERM_EXECUTE ? "X" : ".") << endl;
				begin = addr;
				perm  = p->second.permissions;
				size  = 0;
			}
			size  += VirtualMemory::BLOCK_SIZE;
			total += VirtualMemory::BLOCK_SIZE;

			if (++p == blocks.end()) {
				cout << setw(8) << begin << " - " << setw(8) << begin + size - 1 << ": ";
				cout << (perm & IMemory::PERM_READ    ? "R" : ".");
				cout << (perm & IMemory::PERM_WRITE   ? "W" : ".");
				cout << (perm & IMemory::PERM_EXECUTE ? "X" : ".") << endl;
			}
		}
		total += VirtualMemory::BLOCK_SIZE;
	}

	// Print total memory usage
	int mod = 0;
	while (total >= 1024 && mod < 4)
	{
		total /= 1024;
		mod++;
	}
	static const char* Mods[] = { "B", "kB", "MB", "GB", "TB" };

	cout << endl << "Total allocated memory: " << dec << total << " " << Mods[mod] << endl;

    return true;
}

/**
 ** Network functions
 **/
static bool cmd_network_help(Object* obj, const vector<string>& arguments )
{
    if (dynamic_cast<Network*>(obj) == NULL) return false;

    cout <<
    "- read <network-component>\n"
    "Reads various registers and buffers from the Network component.\n";
    return true;
}

static bool cmd_network_read( Object* obj, const vector<string>& arguments )
{
    const Network* network = dynamic_cast<Network*>(obj);
    if (network == NULL) return false;

    cout << "Shareds:" << endl;
    if (network->m_sharedRequest.fid  != INVALID_GFID) cout << "* Requesting shared " << network->m_sharedRequest.addr.index  << " for G" << network->m_sharedRequest.fid << " from previous processor" << endl;
    if (network->m_sharedResponse.fid != INVALID_GFID)
    {
        if (network->m_sharedResponse.value.m_state == RST_FULL) {
            cout << "* Sending shared "    << network->m_sharedResponse.addr.index << " for G" << network->m_sharedResponse.fid << " to next processor" << endl;
        } else {
            cout << "* Reading shared "    << network->m_sharedResponse.addr.index << " for G" << network->m_sharedResponse.fid << endl;
        }
    }
    if (network->m_sharedReceived.fid != INVALID_GFID) cout << "* Received shared "   << network->m_sharedReceived.addr.index << " for G" << network->m_sharedReceived.fid << " from previous processor" << endl;
    cout << endl;

    cout << "Token:" << endl;
    if (network->m_hasToken.read())       cout << "* Processor has token (" << network->m_lockToken << " outstanding creates)" << endl;
    if (network->m_wantToken.read())      cout << "* Processor wants token" << endl;
    if (network->m_nextWantsToken.read()) cout << "* Next processor wants token" << endl;
    cout << endl;

    cout << "Families and threads:" << endl;
    if (network->m_createState != Network::CS_PROCESSING_NONE) {
        cout << "* Processing " << (network->m_createState == Network::CS_PROCESSING_LOCAL ? "local" : "remote") << " create for F" << network->m_createFid
             << ", global " << network->m_global.addr.str() << endl;
    }

    if (network->m_reservation.isLocalFull())   cout << "* Local family reservation for " << network->m_reservation.readLocal().fid << endl;
    if (network->m_reservation.isSendingFull()) cout << "* Forwarding family reservation for " << network->m_reservation.readSending().fid << endl;
    if (network->m_reservation.isRemoteFull())
    {
        cout << "* Received family reservation for " << network->m_reservation.readRemote().fid << " (";
        cout << (network->m_reservation.isRemoteProcessed() ? "processed" : "not processed") << ")" << endl;
    }

    if (network->m_unreservation.isLocalFull())   cout << "* Local family unreservation for " << network->m_unreservation.readLocal().fid << endl;
    if (network->m_unreservation.isSendingFull()) cout << "* Forwarding family unreservation for " << network->m_unreservation.readSending().fid << endl;
    if (network->m_unreservation.isRemoteFull())
    {
        cout << "* Received family unreservation for " << network->m_unreservation.readRemote().fid << " (";
        cout << (network->m_unreservation.isRemoteProcessed() ? "processed" : "not processed") << ")" << endl;
    }

    if (!network->m_completedFamily.empty()) cout << "* Local family completion of " << network->m_completedFamily.read() << endl;
    if (!network->m_completedThread.empty()) cout << "* Local thread completion of " << network->m_completedThread.read() << endl;
    if (!network->m_cleanedUpThread.empty()) cout << "* Local thread cleanup of " << network->m_cleanedUpThread.read() << endl;

    return true;
}

/**
 ** Allocator functions
 **/
static bool cmd_allocator_help(Object* obj, const vector<string>& arguments )
{
    if (dynamic_cast<Allocator*>(obj) == NULL) return false;

    cout <<
    "- read <allocator-component>\n"
    "Reads the local and remote create queues.\n";
    return true;
}

static bool cmd_allocator_read( Object* obj, const vector<string>& arguments )
{
    const Allocator* alloc = dynamic_cast<Allocator*>(obj);
    if (alloc == NULL) return false;

	Buffer<LFID>                    creates = alloc->getCreateQueue();
    Buffer<TID>                     cleanup = alloc->getCleanupQueue();
	Buffer<Allocator::AllocRequest> allocs  = alloc->GetAllocationQueue();
    
    cout << "Allocation queue: " << endl;
    if (allocs.empty())
    {
        cout << "Empty" << endl;
    }
    else
    {
        while (!allocs.empty())
        {
			const Allocator::AllocRequest& req = allocs.front();
			cout << "T" << req.parent << ":R" << hex << uppercase << setw(4) << setfill('0') << req.reg;
            allocs.pop();
            if (!allocs.empty())
            {
                cout << ", ";
            }
        }
        cout << endl;
    }
	cout << endl;

    cout << "Create queue: " << dec << endl;
    if (creates.empty())
    {
        cout << "Empty" << endl;
    }
    else
    {
		LFID fid = creates.front();
        for (int i = 0; !creates.empty(); i++)
        {
			cout << "F" << creates.front();
            creates.pop();
			if (!creates.empty())
			{
				cout << ", ";
			}
        }
        cout << endl;
		cout << "Create state for F" << fid << ": ";
		switch (alloc->GetCreateState())
		{
			case Allocator::CREATE_INITIAL:				 cout << "Initial"; break;
			case Allocator::CREATE_LOADING_LINE:		 cout << "Loading cache-line"; break;
			case Allocator::CREATE_LINE_LOADED:			 cout << "Cache-line loaded"; break;
			case Allocator::CREATE_GETTING_TOKEN:		 cout << "Getting token"; break;
			case Allocator::CREATE_HAS_TOKEN:			 cout << "Received token"; break;
			case Allocator::CREATE_RESERVING_FAMILY:	 cout << "Reserving family"; break;
			case Allocator::CREATE_BROADCASTING_CREATE:	 cout << "Broadcasting create"; break;
			case Allocator::CREATE_ALLOCATING_REGISTERS: cout << "Allocating registers"; break;
		}
		cout << endl;
    }
    cout << endl;

    cout << "Cleanup queue: " << endl;
    if (cleanup.empty())
    {
        cout << "Empty" << endl;
    }
    else
    {
        while (!cleanup.empty())
        {
            cout << "T" << cleanup.front();
            cleanup.pop();
            if (!cleanup.empty())
            {
                cout << ", ";
            }
        }
        cout << endl;
    }

    return true;
}

/**
 ** I-Cache functions
 **/
static bool cmd_icache_help(Object* obj, const vector<string>& arguments )
{
    if (dynamic_cast<ICache*>(obj) == NULL) return false;

    cout <<
    "- info <icache-component>\n"
    "Returns information and statistics about the instruction cache.\n\n"
    "- read <icache-component>\n"
    "Reads the cache-lines of the instruction cache.\n";
    return true;
}

static bool cmd_icache_info( Object* obj, const vector<string>& arguments )
{
    const ICache* cache = dynamic_cast<ICache*>(obj);
    if (cache == NULL) return false;

    size_t assoc    = cache->getAssociativity();
    size_t nLines   = cache->getNumLines();
    size_t lineSize = cache->getLineSize();

    cout << "Cache type:          ";
    if (assoc == 1) {
        cout << "Direct mapped" << endl;
    } else if (assoc == nLines) {
        cout << "Fully associative" << endl;
    } else {
        cout << dec << assoc << "-way set associative" << endl;
    }

    cout << "Cache size:          " << dec << (lineSize * nLines) << " bytes" << endl;
    cout << "Cache line size:     " << dec << lineSize << " bytes" << endl;
    cout << endl;

    uint64_t hits    = cache->getNumHits();
    uint64_t misses  = cache->getNumMisses();
    cout << "Current hit rate:    ";
    if (hits + misses > 0) {
        cout << dec << hits * 100 / (hits + misses) << "%" << endl;
    } else {
        cout << "N/A" << endl;
    }
    return true;
}

static bool cmd_icache_read( Object* obj, const vector<string>& arguments )
{
    const ICache* cache = dynamic_cast<ICache*>(obj);
    if (cache == NULL) return false;

    for (size_t s = 0; s < cache->getNumSets(); s++)
    {
        cout << setw(3) << setfill(' ') << dec << s;
        for (size_t a = 0; a < cache->getAssociativity(); a++)
        {
            const ICache::Line& line = cache->getLine(s * cache->getAssociativity() + a);
            cout << " | ";
            if (!line.used) {
                cout << "    -     ";
            } else {
                cout << setw(8) << setfill('0') << hex << (line.tag * cache->getNumSets() + s) * cache->getLineSize() << " "
                     << (line.waiting.head != INVALID_TID ? "L" : " ");
            }
        }
        cout << endl;
    }

    return true;
}

/**
 ** D-Cache functions
 **/
static bool cmd_dcache_help(Object* obj, const vector<string>& arguments )
{
    if (dynamic_cast<DCache*>(obj) == NULL) return false;

    cout <<
    "- info <dcache-component>\n"
    "Returns information and statistics about the data cache.\n\n"
    "- read <dcache-component>\n"
    "Reads the cache-lines of the data cache.\n";
    return true;
}

static bool cmd_dcache_info( Object* obj, const vector<string>& arguments )
{
    const DCache* cache = dynamic_cast<DCache*>(obj);
    if (cache == NULL) return false;

    size_t assoc    = cache->getAssociativity();
    size_t nLines   = cache->getNumLines();
    size_t lineSize = cache->getLineSize();

    cout << "Cache type:          ";
    if (assoc == 1) {
        cout << "Direct mapped" << endl;
    } else if (assoc == nLines) {
        cout << "Fully associative" << endl;
    } else {
        cout << dec << assoc << "-way set associative" << endl;
    }

    cout << "Cache size:          " << dec << (lineSize * nLines) << " bytes" << endl;
    cout << "Cache line size:     " << dec << lineSize << " bytes" << endl;
    cout << endl;

    uint64_t hits    = cache->getNumHits();
    uint64_t misses  = cache->getNumMisses();
    cout << "Current hit rate:    ";
    if (hits + misses > 0) {
        cout << dec << hits * 100 / (hits + misses) << "%" << endl;
    } else {
        cout << "N/A" << endl;
    }
    cout << "Pending requests:    " << cache->getNumPending() << endl;
    return true;
}

static bool cmd_dcache_read( Object* obj, const vector<string>& arguments )
{
    const DCache* cache = dynamic_cast<DCache*>(obj);
    if (cache == NULL) return false;

	for (size_t s = 0; s < cache->getNumSets(); s++)
    {
        cout << setw(3) << setfill(' ') << dec << s;
        for (size_t a = 0; a < cache->getAssociativity(); a++)
        {
            const DCache::Line& line = cache->getLine(s * cache->getAssociativity() + a);
            cout << " | ";
            if (line.state == DCache::LINE_INVALID) {
                cout << "     -    ";
            } else {
				cout << setw(8) << setfill('0') << hex << (line.tag * cache->getNumSets() + s) * cache->getLineSize() << " "
                    << (line.state == DCache::LINE_LOADING ? 'L' : ' ');
            }
        }
        cout << endl;
    }

    return true;
}

/**
 ** Family Table functions
 **/
static bool cmd_families_help(Object* obj, const vector<string>& arguments )
{
    if (dynamic_cast<FamilyTable*>(obj) == NULL) return false;

    cout <<
    "- read <family-table-component>\n"
    "Reads the local and global family table entries.\n";
    return true;
}

// Read the global and local family table
static bool cmd_families_read( Object* obj, const vector<string>& arguments )
{
    const FamilyTable* table = dynamic_cast<FamilyTable*>(obj);
    if (table == NULL) return false;

    static const char* FamilyStates[] = {"", "ALLOCATED", "IDLE", "ACTIVE", "KILLED"};

	cout << "    |    PC    |   Allocated   | P/A/Rd/Sh | Parent | GFID | State" << endl;
    cout << "----+----------+---------------+-----------+--------+------+-----------" << endl;
    
    cout << setfill(' ') << right;
	
	const vector<Family>& families = table->GetFamilies();
	for (size_t i = 0; i < families.size(); i++)
    {
        const Family& family = families[i];

        cout << dec << setw(3) << i << " | ";
        if (family.state != FST_EMPTY)
        {
			if (family.state != FST_ALLOCATED)
			{
	            cout << hex
	                 << setw(8) << family.pc << " | "
				     << dec
				     << setw(3) << family.dependencies.numThreadsAllocated << "/"
					 << setw(3) << family.physBlockSize << " ("
					 << setw(3) << family.virtBlockSize << ") | "
				     << !family.dependencies.prevTerminated << "/"
					 << !family.dependencies.allocationDone << "/"
					 << setw(2) << family.dependencies.numPendingReads << "/"
					 << setw(2) << family.dependencies.numPendingShareds << " | " << right;
			}
			else {
	            cout << "   -     |       -       |     -     | ";
			}

			if (family.parent.tid == INVALID_TID) {
				cout << "  -    | ";
			} else {
				cout << setw(3) << (int)family.parent.tid << "@" << setw(2) << family.parent.pid << " | ";
			}

			if (family.state != FST_ALLOCATED)
			{
				if (family.gfid == INVALID_GFID) {
					cout << " -  ";
				} else {
					cout << "G" << setw(2) << setfill('0') << right << family.gfid << " ";
				}
			}
			else {
	            cout << " -  ";
			}
            cout << " | " << FamilyStates[family.state];
        }
        else
        {
            cout << "         |               |           |        |      | ";
        }
        cout << endl;
    }
	cout << endl;
	
    return true;
}

/**
 ** Thread Table functions
 **/
static bool cmd_threads_help(Object* obj, const vector<string>& arguments )
{
    if (dynamic_cast<ThreadTable*>(obj) == NULL) return false;

    cout <<
    "- read <thread-table-component>\n"
    "Reads the thread table entries.\n";
    return true;
}

static bool cmd_threads_read( Object* obj, const vector<string>& arguments )
{
    const ThreadTable* table = dynamic_cast<ThreadTable*>(obj);
    if (table == NULL) return false;

    static const char* ThreadStates[] = {
        "", "WAITING", "ACTIVE", "RUNNING", "SUSPENDED", "UNUSED", "KILLED"
    };

    cout << "    |    PC    | Fam | Index | Prev | Next | Int. | Flt. | Flags | WR | State" << endl;
    cout << "----+----------+-----+-------+------+------+------+------+-------+----+----------" << endl;
    for (TID i = 0; i < table->getNumThreads(); i++)
    {
        cout << dec << setw(3) << setfill(' ') << i << " | ";
        const Thread& thread = (*table)[i];

        if (thread.state != TST_EMPTY)
        {
            cout << setw(8) << setfill('0') << hex << thread.pc << " | ";
            cout << "F" << setw(2) << thread.family << " | ";
            cout << setw(5) << dec << setfill(' ') << thread.index << " | ";
            if (thread.prevInBlock != INVALID_TID) cout << dec << setw(4) << setfill(' ') << thread.prevInBlock; else cout << "   -";
            cout << " | ";
            if (thread.nextInBlock != INVALID_TID) cout << dec << setw(4) << setfill(' ') << thread.nextInBlock; else cout << "   -";
            cout << " | ";

            for (RegType type = 0; type < NUM_REG_TYPES; type++)
            {
                if (thread.regs[type].base != INVALID_REG_INDEX)
                    cout << setw(4) << setfill('0') << hex << thread.regs[type].base;
                else
                    cout << "  - ";
                cout << " | ";
            }

            cout << (thread.dependencies.prevCleanedUp ? 'P' : '.')
                 << (thread.dependencies.killed        ? 'K' : '.')
                 << (thread.dependencies.nextKilled    ? 'N' : '.')
                 << (thread.isLastThreadInBlock        ? 'L' : '.')
                 << "  | "
                 << setw(2) << setfill(' ') << thread.dependencies.numPendingWrites
                 << " | ";

            cout << ThreadStates[thread.state];
        }
        else
        {
            cout << "         |     |       |      |      |      |      |       |    |";
        }
        cout << endl;
    }
    return true;
}

/**
 ** Pipeline functions
 **/
static bool cmd_pipeline_help(Object* obj, const vector<string>& arguments )
{
    if (dynamic_cast<Pipeline*>(obj) == NULL) return false;

    cout <<
    "- read <pipeline-component>\n"
    "Reads the contents of the pipeline latches.\n";
    return true;
}

// Construct a string representation of a register value
static string MakeRegValue(const RegType& type, const RegValue& value)
{
    stringstream ss;

    switch (value.m_state)
    {
        case RST_INVALID:   ss << "N/A";     break;
		case RST_PENDING:   ss << "Pending"; break;
        case RST_EMPTY:     ss << "Empty";   break;
		case RST_WAITING:   ss << "Waiting (" << setw(4) << setfill('0') << value.m_tid << "h)"; break;
        case RST_FULL:
            if (type == RT_INTEGER)
                ss << setw(16) << setfill('0') << hex << value.m_integer << "h";
            else
                ss << setprecision(16) << fixed << value.m_float.todouble();
            break;
    }
	
	string ret = ss.str();
	if (ret.length() > 16) {
		ret = ret.substr(0,16);
	}
    return ret;
}

static ostream& operator<<(ostream& out, const RemoteRegAddr& rreg) {
    if (rreg.fid != INVALID_GFID) {
        out << hex << setw(2) << setfill('0') << rreg.reg.str() << " @ " << rreg.fid;
    } else {
        out << "N/A";
    }
    return out;
}

// Read the pipeline latches
static bool cmd_pipeline_read( Object* obj, const vector<string>& arguments )
{
    const Pipeline* pipe = dynamic_cast<Pipeline*>(obj);
    if (pipe == NULL) return false;

    // Get the stages and latches
    const Pipeline::FetchStage&       fetch   = (const Pipeline::FetchStage&)    pipe->getStage(0);
    const Pipeline::DecodeStage&      decode  = (const Pipeline::DecodeStage&)   pipe->getStage(1);
    const Pipeline::ReadStage&        read    = (const Pipeline::ReadStage&)     pipe->getStage(2);
    const Pipeline::ExecuteStage&     execute = (const Pipeline::ExecuteStage&)  pipe->getStage(3);
    const Pipeline::MemoryStage&      memory  = (const Pipeline::MemoryStage&)   pipe->getStage(4);
    const Pipeline::FetchDecodeLatch&     fdlatch = *(const Pipeline::FetchDecodeLatch*)    fetch  .getOutput();
    const Pipeline::DecodeReadLatch&      drlatch = *(const Pipeline::DecodeReadLatch*)     decode .getOutput();
    const Pipeline::ReadExecuteLatch&     relatch = *(const Pipeline::ReadExecuteLatch*)    read   .getOutput();
    const Pipeline::ExecuteMemoryLatch&   emlatch = *(const Pipeline::ExecuteMemoryLatch*)  execute.getOutput();
    const Pipeline::MemoryWritebackLatch& mwlatch = *(const Pipeline::MemoryWritebackLatch*)memory .getOutput();

    // Fetch stage
    cout << "Fetch stage" << endl;
    cout << " |" << endl;
    if (fdlatch.empty())
    {
        cout << " | (Empty)" << endl;
    }
    else
    {
        cout << " | LFID: "  << dec << fdlatch.fid
             << "    TID: "  << dec << fdlatch.tid << right
             << "    PC: "   << hex << setw(8) << setfill('0') << fdlatch.pc << "h"
             << "    Annotation: " << ((fdlatch.kill) ? "End" : (fdlatch.swch ? "Switch" : "None")) << endl 
             << " |" << endl;
        cout << " | Instr: " << hex << setw(8) << setfill('0') << fdlatch.instr << "h" << endl;
    }
    cout << " v" << endl;

    // Decode stage
    cout << "Decode stage" << endl;
    cout << " |" << endl;
    if (drlatch.empty())
    {
        cout << " | (Empty)" << endl;
    }
    else
    {
        cout << " | LFID: "  << dec << drlatch.fid
             << "    TID: "  << dec << drlatch.tid
             << "    PC: "   << hex << setw(8) << setfill('0') << drlatch.pc << "h"
             << "    Annotation: " << ((drlatch.kill) ? "End" : (drlatch.swch ? "Switch" : "None")) << endl 
             << " |" << endl;
        cout << " | Opcode:       " << hex << setw(2)  << setfill('0') << (int)drlatch.opcode << "h" << endl;
        cout << " | Function:     " << hex << setw(4)  << setfill('0') << drlatch.function << "h" << endl;
        cout << " | Ra:           " << drlatch.Ra << "    Rra: " << drlatch.Rra << endl;
        cout << " | Rb:           " << drlatch.Rb << "    Rrb: " << drlatch.Rrb << endl;
        cout << " | Rc:           " << drlatch.Rc << "    Rrc: " << drlatch.Rrc << endl;
        cout << " | Displacement: " << hex << setw(16) << setfill('0') << drlatch.displacement << "h" << endl;
        cout << " | Literal:      " << hex << setw(16) << setfill('0') << drlatch.literal << "h" << endl;
    }
    cout << " v" << endl;

    // Read stage
    cout << "Read stage" << endl;
    cout << " |" << endl;
    if (relatch.empty())
    {
        cout << " | (Empty)" << endl;
    }
    else
    {
        RegType type = RT_INTEGER;

        cout << " | LFID: "  << dec << relatch.fid
             << "    TID: "  << dec << relatch.tid
             << "    PC: "   << hex << setw(8) << setfill('0') << relatch.pc << "h"
             << "    Annotation: " << ((relatch.kill) ? "End" : (relatch.swch ? "Switch" : "None")) << endl 
             << " |" << endl;
        cout << " | Opcode:       " << hex << setw(2)  << setfill('0') << (int)relatch.opcode << "h" << endl;
        cout << " | Function:     " << hex << setw(4)  << setfill('0') << relatch.function << "h" << endl;
        cout << " | Rav:          " << MakeRegValue(type, relatch.Rav) << endl;
        cout << " | Rbv:          " << MakeRegValue(type, relatch.Rbv) << endl;
        cout << " | Rc:           " << relatch.Rc << "    Rrc: " << relatch.Rrc << endl;
        cout << " | Displacement: " << hex << setw(16) << setfill('0') << relatch.displacement << "h" << endl;
    }
    cout << " v" << endl;

    // Execute stage
    cout << "Execute stage" << endl;
    cout << " |" << endl;
    if (emlatch.empty())
    {
        cout << " | (Empty)" << endl;
    }
    else
    {
        cout << " | LFID: "  << dec << emlatch.fid
             << "    TID: "  << dec << emlatch.tid
             << "    PC: "   << hex << setw(8) << setfill('0') << emlatch.pc << "h"
             << "    Annotation: " << ((emlatch.kill) ? "End" : (emlatch.swch ? "Switch" : "None")) << endl
             << " |" << endl;
        cout << " | Rc:        " << emlatch.Rc << "    Rrc: " << emlatch.Rrc << endl;
        cout << " | Rcv:       " << MakeRegValue(emlatch.Rc.type, emlatch.Rcv) << endl;
        if (emlatch.size == 0)
        {
            // No memory operation
            cout << " | Operation: N/A" << endl;
            cout << " | Address:   N/A" << endl;
            cout << " | Size:      N/A" << endl;
        }
        else
        {
            cout << " | Operation: " << (emlatch.Rcv.m_state == RST_FULL ? "Store" : "Load") << endl;
            cout << " | Address:   " << hex << setw(16) << setfill('0') << emlatch.address << "h" << endl;
            cout << " | Size:      " << hex << setw(16) << setfill('0') << emlatch.size    << "h" << endl;
        }
    }
    cout << " v" << endl;
    
    // Memory stage
    cout << "Memory stage" << endl;
    cout << " |" << endl;
    if (mwlatch.empty())
    {
        cout << " | (Empty)" << endl;
    }
    else
    {
        cout << " | LFID: "  << dec << mwlatch.fid
             << "    TID: "  << dec << mwlatch.tid
             << "    PC: "   << hex << setw(8) << setfill('0') << mwlatch.pc << "h"
             << "    Annotation: " << ((mwlatch.kill) ? "End" : (mwlatch.swch ? "Switch" : "None")) << endl
             << " |" << endl;
        cout << " | Rc:   " << mwlatch.Rc << "    Rrc: " << mwlatch.Rrc << endl;
        cout << " | Rcv:  " << MakeRegValue(mwlatch.Rc.type, mwlatch.Rcv) << endl;
    }
    cout << " v" << endl;

    // Writeback stage
    cout << "Writeback stage" << endl;

    return true;
}

/**
 ** RAU functions
 **/
static bool cmd_rau_help(Object* obj, const vector<string>& arguments )
{
    if (dynamic_cast<RAUnit*>(obj) == NULL) return false;

    cout <<
    "- read <raunit-component>\n"
    "Reads the allocation of registers from the register allocation unit.\n";
    return true;
}

static const char* TypeNames[NUM_REG_TYPES] = {"Integer", "Float"};
static const char* StateNames[5] = {"", "Empty", "Pending", "Waiting", "Full"};

// Read the Register Allocation Unit
static bool cmd_rau_read( Object* obj, const vector<string>& arguments )
{
    const RAUnit* rau = dynamic_cast<RAUnit*>(obj);
    if (rau == NULL) return false;

    RegType type = (arguments.size() > 0 && arguments[0] == "float") ? RT_FLOAT : RT_INTEGER;
    const RAUnit::List& list = rau->getList(type);
    const RegSize blockSize  = rau->getBlockSize(type);

    cout << TypeNames[type] << " registers:" << endl;
    for (size_t next, entry = 0; entry < list.size(); entry = next)
    {
        cout << hex << setw(4) << entry * blockSize << "h - " << setw(4);

        if (list[entry].first != 0) {
            next = entry + list[entry].first;
            cout << (next * blockSize) - 1 << "h: Allocated to " << list[entry].second << endl;
        } else {
            for (next = entry + 1; next < list.size() && list[next].first == 0; next++);
            cout << (next * blockSize) - 1 << "h: Free" << endl;
        }
    }
    cout << endl;
    return true;
}

struct REGINFO
{
    LFID     fid;
    TID      tid;
    RegGroup group;

    REGINFO() : fid(INVALID_LFID), tid(INVALID_TID) {}
};

/**
 ** RegisterFile functions
 **/
static bool cmd_regs_help(Object* obj, const vector<string>& arguments )
{
    if (dynamic_cast<RegisterFile*>(obj) == NULL) return false;

    cout <<
    "- read <registerfile-component>\n"
    "Reads the integer and floating point registers from the register file.\n";
    return true;
}

// Read the register files
static bool cmd_regs_read( Object* obj, const vector<string>& arguments )
{
    const RAUnit*       rau     = NULL;
    const FamilyTable*  ftable  = NULL;
    const Allocator*    alloc   = NULL;
    const RegisterFile* regfile = dynamic_cast<RegisterFile*>(obj);
    if (regfile == NULL) return false;

    // Find the RAUnit and FamilyTable in the same processor
    const Object* proc = regfile->getParent();
    if (proc != NULL)
    {
        for (unsigned int i = 0; i < proc->getNumChildren(); i++)
        {
            const Object* child = proc->getChild(i);
            if (rau    == NULL) rau    = dynamic_cast<const RAUnit*>(child);
            if (ftable == NULL) ftable = dynamic_cast<const FamilyTable*>(child);
            if (alloc  == NULL) alloc  = dynamic_cast<const Allocator*>(child);
        }
    }

    RegType type = (arguments.size() > 0 && arguments[0] == "float") ? RT_FLOAT : RT_INTEGER;
    RegSize size = regfile->getSize(type);

    vector<REGINFO> regs(size);
    if (rau != NULL)
    {
        const RAUnit::List& list = rau->getList(type);
        const RegSize blockSize  = rau->getBlockSize(type);
        for (size_t i = 0; i < list.size();)
        {
            if (list[i].first != 0)
            {
                for (size_t j = 0; j < list[i].first * blockSize; j++)
                {
                    regs[i * blockSize + j].fid = list[i].second;
                }
                i += list[i].first;
            }
            else i++;
        }
    }

    if (ftable != NULL && alloc != NULL)
    {
        for (size_t i = 0; i < regs.size(); i++)
        {
            if (regs[i].fid != INVALID_LFID)
            {
                const Family& family = (*ftable)[regs[i].fid];
                for (TID t = 0; t < family.physBlockSize; t++)
                {
                }
            }
        }
    }

    cout << "      |  State  |       Value      | Fam | Thread | Type" << endl;
    cout << "------+---------+------------------+-----+--------+-----------" << endl;
    for (RegIndex i = size; i > 0; i--)
    {
        RegValue value;
        RegAddr  addr = MAKE_REGADDR(type, i - 1);
        LFID      fid = regs[i - 1].fid;
        regfile->readRegister(addr, value);
        cout << addr << " | " << setw(7) << setfill(' ') << StateNames[value.m_state] << " | ";
        
		stringstream ss;
        switch (value.m_state)
        {
        case RST_FULL:
            switch (type)
            {
            case RT_INTEGER: ss << setw(16) << setfill('0') << hex << value.m_integer; break;
            case RT_FLOAT:   ss << setprecision(16) << fixed << value.m_float.todouble(); break;
            }
            break;

        case RST_WAITING:
            ss << "      " << setw(4) << setfill('0') << hex << value.m_tid << "      "; break;
            break;

        case RST_INVALID:
        case RST_EMPTY:
		case RST_PENDING:
            ss << setw(16) << " ";
            break;
        }

		cout << ss.str().substr(0, 16) << " | ";
        if (fid != INVALID_LFID) cout << "F" << setw(2) << setfill('0') << fid; else cout << "   ";
        cout << " |  ";

		RegGroup group = RG_LOCAL;
		TID      tid   = (fid != INVALID_LFID) ? alloc->GetRegisterType(fid, addr, &group) : INVALID_TID;
		if (tid != INVALID_TID) {
			cout << "T" << setw(4) << setfill('0') << tid;
		} else {
			cout << "  -  ";
		}
		cout << " | ";
		switch (group)
		{
			case RG_GLOBAL:    cout << "Global"; break;
			case RG_DEPENDENT: cout << "Dependent"; break;
			case RG_SHARED:    cout << "Shared"; break;
			case RG_LOCAL:     
				if (tid != INVALID_TID) cout << "Local";
				break;
		}
		cout << endl;
    }

    return true;
}

//
// The list with all supported non-standard commands
//
const COMMAND Commands[] = {
    {"help", cmd_allocator_help },
    {"help", cmd_pipeline_help },
    {"help", cmd_network_help },
    {"help", cmd_mem_help },
    {"help", cmd_icache_help },
    {"help", cmd_dcache_help },
    {"help", cmd_regs_help },
    {"help", cmd_families_help },
    {"help", cmd_threads_help },
    {"help", cmd_rau_help },

    {"read", cmd_allocator_read },
    {"read", cmd_pipeline_read },
    {"read", cmd_network_read },
    {"read", cmd_mem_read },
    {"read", cmd_icache_read },
    {"read", cmd_dcache_read },
    {"read", cmd_regs_read },
    {"read", cmd_families_read },
    {"read", cmd_threads_read },
    {"read", cmd_rau_read },

    {"info", cmd_icache_info },
    {"info", cmd_dcache_info },
    {"info", cmd_mem_info },
    {NULL}
};
