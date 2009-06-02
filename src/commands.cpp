#include "commands.h"
#include "Processor.h"
#include "SimpleMemory.h"
#include "ParallelMemory.h"
#include "BankedMemory.h"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <cstdlib>
using namespace std;
using namespace Simulator;

/**
 ** Memory component functions
 **/
static bool cmd_mem_help(Object* obj, const vector<string>& /* arguments */)
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
    RequestQueue requests = mem->GetRequests();
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
                cout << cpu->GetName() << " | ";
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
    if (mem->GetRequests().empty() || mem->GetRequests().front().done == 0) {
        cout << "N/A";
    } else {
        cout << dec << setw(8) << mem->GetRequests().front().done;
    }
    cout << endl << endl;
}

static void cmd_parallelmem_requests(ParallelMemory* mem)
{
    // Display in-flight requests
    for (size_t i = 0; i < mem->GetNumPorts(); ++i) {
        cout << "    Done   | Address  | Size | CID  ";
    }
    cout << endl;
    for (size_t i = 0; i < mem->GetNumPorts(); ++i) {
        cout << "-----------+----------+------+----- ";
    }
    cout << endl;

	vector<multimap<CycleNo, ParallelMemory::Request>::const_iterator> iters;
	for (size_t i = 0; i < mem->GetNumPorts(); ++i)
	{
		iters.push_back(mem->GetPort(i).m_inFlight.begin());
	}

	for (size_t y = 0; y < mem->GetConfig().width; ++y)
	{
		for (size_t x = 0; x < mem->GetNumPorts(); ++x)
		{
			multimap<CycleNo, ParallelMemory::Request>::const_iterator& p = iters[x];

			const ParallelMemory::Port& port = mem->GetPort(x);
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

				cout << setw(9) << dec << setfill(' ') << p->first << " | "
					 << setw(8) << hex << setfill('0') << right << req.address << " | "
				     << setw(4) << dec << setfill(' ') << req.data.size << " | ";

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

static void print_bankedmem_pipelines(const vector<BankedMemory::Bank> banks, const BankedMemory::Pipeline BankedMemory::Bank::*queue)
{
    for (size_t i = 0; i < banks.size(); ++i) {
        cout << "    Done   | Address  | Size | CID  ";
    }
    cout << endl;
    for (size_t i = 0; i < banks.size(); ++i) {
        cout << "-----------+----------+------+----- ";
    }
    cout << endl;
    
	vector<BankedMemory::Pipeline::const_iterator> iters;
	size_t length = 0;
	for (size_t i = 0; i < banks.size(); ++i)
	{
		iters.push_back((banks[i].*queue).begin());
		length = max(length, (banks[i].*queue).size());
	}

	for (size_t y = 0; y < length; ++y)
	{
		for (size_t x = 0; x < banks.size(); ++x)
		{
			BankedMemory::Pipeline::const_iterator& p = iters[x];

			if (p != (banks[x].*queue).end())
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
    const vector<BankedMemory::Bank>& banks = mem->GetBanks();

    print_bankedmem_pipelines(banks, &BankedMemory::Bank::incoming);
    
    // Print the banks
    for (size_t i = 0; i < banks.size(); ++i) {
        cout << "    Done   | Address  | Size | CID  ";
    }
    cout << endl;
    for (size_t i = 0; i < banks.size(); ++i) {
        cout << "-----------+----------+------+----- ";
    }
    cout << endl;
    for (size_t i = 0; i < banks.size(); ++i)
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
    
    print_bankedmem_pipelines(banks, &BankedMemory::Bank::outgoing);
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
        addr = (MemAddr)strtoull( arguments[0].c_str(), &endptr, 0 );
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

    try {
        // Read the data
        vector<uint8_t> buf((size_t)size);
        mem->Read(addr, &buf[0], size);
    
        // Print it, 16 bytes per row
        for (MemAddr y = start; y < end; y += 16)
        {
            // The address
            cout << setw(8) << hex << setfill('0') << y << " | ";

            // The bytes
            for (MemAddr x = 0; x < 16; ++x)
            {
                if (y + x >= addr && y + x < addr + size)
                    cout << hex << setw(2) << setfill('0') << (unsigned int)buf[(size_t)(y + x - addr)];
                else
                    cout << "  ";
                if (x == 7) cout << "  ";
                cout << " ";
            }
            cout << "| ";

            // The bytes, as characters
            for (MemAddr x = 0; x < 16; ++x)
            {
                char c = buf[(size_t)(y + x - addr)];
                if (y + x >= addr && y + x < addr + size)
                    c = (isprint(c) ? c : '.');
                else c = ' ';
                cout << c;
            }

            cout << endl;
        }
    }
    catch (exception &e)
    {
        cout << "An exception occured while reading the memory:" << endl;
        cout << e.what() << endl;
    }
    return true;
}

// Reads statistics for the memory
// Usage: info <mem-component>
static bool cmd_mem_info(Object* obj, const vector<string>& /* arguments */)
{
    VirtualMemory* mem = dynamic_cast<VirtualMemory*>(obj);
    if (mem == NULL) return false;

	const VirtualMemory::RangeMap& ranges = mem->GetRangeMap();
	cout << "Reserved memory ranges:" << endl
		 << "------------------------" << endl;

	cout << hex << setfill('0');

    MemSize total = 0;
    VirtualMemory::RangeMap::const_iterator p = ranges.begin();
    if (p != ranges.end())
    {
        // We have at least one range, walk over all ranges and
        // coalesce neighbouring ranges with similar properties.
        MemAddr begin = p->first;
        int     perm  = p->second.permissions;
        MemAddr size  = 0;

        do
        {
            size  += p->second.size;
            total += p->second.size;
            p++;
            if (p == ranges.end() || p->first > begin + size || p->second.permissions != perm)
            {
                // Different block, or end of blocks
                cout << setw(16) << begin << " - " << setw(16) << begin + size - 1 << ": ";
                cout << (perm & IMemory::PERM_READ    ? "R" : ".");
                cout << (perm & IMemory::PERM_WRITE   ? "W" : ".");
                cout << (perm & IMemory::PERM_EXECUTE ? "X" : ".") << endl;
                if (p != ranges.end())
                {
                    // Different block
                    begin = p->first;
                    perm  = p->second.permissions;
                    size  = 0;
                }
            }
        } while (p != ranges.end());
    }
	
	static const char* Mods[] = { "B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };

	// Print total memory reservation
	int mod;
	for(mod = 0; total >= 1024 && mod < 9; ++mod)
	{
		total /= 1024;
	}
	cout << endl << setfill(' ') << dec;
	cout << "Total reserved memory:  " << setw(4) << total << " " << Mods[mod] << endl;
	
	total = 0;
	const VirtualMemory::BlockMap& blocks = mem->GetBlockMap();
	for (VirtualMemory::BlockMap::const_iterator p = blocks.begin(); p != blocks.end(); ++p)
	{
	    total += VirtualMemory::BLOCK_SIZE;
	}

	// Print total memory usage
	for (mod = 0; total >= 1024 && mod < 4; ++mod)
	{
		total /= 1024;
	}
	cout << "Total allocated memory: " << setw(4) << total << " " << Mods[mod] << endl;
    return true;
}

/**
 ** Network functions
 **/
static bool cmd_network_help(Object* obj, const vector<string>& /* arguments */)
{
    if (dynamic_cast<Network*>(obj) == NULL) return false;

    cout <<
    "- read <network-component>\n"
    "Reads various registers and buffers from the Network component.\n";
    return true;
}

static bool cmd_network_read( Object* obj, const vector<string>& /* arguments */)
{
    const Network* network = dynamic_cast<Network*>(obj);
    if (network == NULL) return false;

    cout << dec;

    cout << "Registers:" << endl;
    if (network->m_registerRequestGroup.out.CanRead())
    {
        const Network::RegisterRequest& request = network->m_registerRequestGroup.out.Read();
        cout << "* Requesting "
             << GetRemoteRegisterTypeString(request.addr.type) << " register "
             << request.addr.reg.str()
             << " from F" << request.addr.fid
             << " on previous processor" << endl;
    }

    if (network->m_registerRequestGroup.in.CanRead())
    {
        const Network::RegisterRequest& request = network->m_registerRequestGroup.in.Read();
        cout << "* Received request for "
             << GetRemoteRegisterTypeString(request.addr.type) << " register "
             << request.addr.reg.str()
             << " in F" << request.addr.fid
             << " from next processor" << endl;
    }
    
    if (network->m_registerResponseGroup.out.CanRead())
    {
        const Network::RegisterResponse& response = network->m_registerResponseGroup.out.Read();
        cout << "* Sending "
             << GetRemoteRegisterTypeString(response.addr.type) << " register "
             << response.addr.reg.str()
             << " to F" << response.addr.fid
             << " on next processor" << endl;
    }

    if (network->m_registerResponseGroup.in.CanRead())
    {
        const Network::RegisterResponse& response = network->m_registerResponseGroup.in.Read();
        cout << "* Received "
             << GetRemoteRegisterTypeString(response.addr.type) << " register "
             << response.addr.reg.str()
             << " in F" << response.addr.fid
             << " from previous processor" << endl;
    }
    
    if (network->m_registerRequestRemote.out.CanRead())
    {
        const Network::RegisterRequest& request = network->m_registerRequestRemote.out.Read();
        cout << "* Requesting "
             << GetRemoteRegisterTypeString(request.addr.type) << " register "
             << request.addr.reg.str()
             << " from F" << request.addr.fid
             << " on P" << request.addr.pid << endl;
    }

    if (network->m_registerRequestRemote.in.CanRead())
    {
        const Network::RegisterRequest& request = network->m_registerRequestRemote.in.Read();
        cout << "* Received request for "
             << GetRemoteRegisterTypeString(request.addr.type) << " register "
             << request.addr.reg.str()
             << " in F" << request.addr.fid
             << " from P" << request.addr.pid << endl;
    }
    
    if (network->m_registerResponseRemote.out.CanRead())
    {
        const Network::RegisterResponse& response = network->m_registerResponseRemote.out.Read();
        cout << "* Sending "
             << GetRemoteRegisterTypeString(response.addr.type) << " register "
             << response.addr.reg.str()
             << " to F" << response.addr.fid
             << " on P" << response.addr.pid << endl;
    }

    if (network->m_registerResponseRemote.in.CanRead())
    {
        const Network::RegisterResponse& response = network->m_registerResponseRemote.in.Read();
        cout << "* Received "
             << GetRemoteRegisterTypeString(response.addr.type) << " register "
             << response.addr.reg.str()
             << " in F" << response.addr.fid
             << " from P" << response.addr.pid << endl;
    }
    cout << endl;

    cout << "Token:" << endl;
    if (network->m_hasToken.Read())       cout << "* Processor has token (used: " << boolalpha << network->m_tokenUsed.Read() << ")" << endl;
    if (network->m_wantToken.Read())      cout << "* Processor wants token" << endl;
    if (network->m_nextWantsToken.Read()) cout << "* Next processor wants token" << endl;
    cout << endl;

    cout << "Families and threads:" << endl;
    if (network->m_terminatedFamily  .CanRead()) cout << "* Sending family termination of F" << network->m_terminatedFamily.Read() << endl;
    if (network->m_synchronizedFamily.CanRead()) cout << "* Sending family synchronization of F" << network->m_synchronizedFamily.Read() << endl;
    if (network->m_completedThread   .CanRead()) cout << "* Sending thread completion of F" << network->m_completedThread.Read() << endl;
    if (network->m_cleanedUpThread   .CanRead()) cout << "* Sending thread cleanup of F" << network->m_cleanedUpThread.Read() << endl;
    if (network->m_delegateRemote    .CanRead()) cout << "* Received delegated create of PC 0x" << hex << network->m_delegateRemote.Read().address << dec << endl;

    return true;
}

/**
 ** Allocator functions
 **/
static bool cmd_allocator_help(Object* obj, const vector<string>& /* arguments */)
{
    if (dynamic_cast<Allocator*>(obj) == NULL) return false;

    cout <<
    "- read <allocator-component>\n"
    "Reads the local and remote create queues.\n";
    return true;
}

static bool cmd_allocator_read( Object* obj, const vector<string>& /* arguments */)
{
    const Allocator* alloc = dynamic_cast<Allocator*>(obj);
    if (alloc == NULL) return false;

    {
        cout << "Family allocation queue: " << endl;
	    Buffer<Allocator::AllocRequest> allocs = alloc->GetFamilyAllocationQueue();
        if (allocs.empty())
        {
            cout << "Empty" << endl;
        }
        else
        {
            while (!allocs.empty())
            {
    			const Allocator::AllocRequest& req = allocs.front();
			    cout << "T" << req.parent << ":R" << hex << uppercase << setw(4) << setfill('0') << req.reg << nouppercase << dec;
                allocs.pop();
                if (!allocs.empty())
                {
                    cout << ", ";
                }
            }
            cout << endl;
        }
	    cout << endl;
	}
	
	{
	    cout << "Thread allocation queue: " << endl;	    
	    const FamilyTable& families = alloc->GetFamilyTable();
        const FamilyQueue& queue    = alloc->GetThreadAllocationQueue();
	    if (queue.head == INVALID_LFID)
	    {
    	    cout << "Empty";
    	}

    	LFID fid = queue.head;
    	while (fid != INVALID_LFID)
    	{
    	    cout << "F" << fid;
    	    if (fid != queue.tail)
    	    {
        	    cout << ", ";
    	    }
    	    fid = families[fid].next;
    	}
    	cout << endl << endl;
    }
    
    {    
        cout << "Current exclusive family: ";
        LFID fid_ex = alloc->GetExclusiveFamily();
        if (fid_ex == INVALID_LFID) {
            cout << "None";
        } else {
            cout << "F" << fid_ex;
        }
        cout << endl << endl;
    }
    
	{
	    const struct {
	        const char*         name;
	        const Buffer<LFID>& queue;
	    } queues[2] = {
	        {"Non-exclusive", alloc->GetCreateQueue()          },
	        {"Exclusive",     alloc->GetExclusiveCreateQueue() }
	    };
	    
	    for (size_t i = 0; i < 2; i++)
	    {
            cout << queues[i].name << " create queue: " << dec << endl;
	        Buffer<LFID> creates = queues[i].queue;
            if (creates.empty())
            {
                cout << "Empty" << endl;
            }
            else
            {
    		    LFID fid = creates.front();
                for (int i = 0; !creates.empty(); ++i)
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
			        case Allocator::CREATE_ACQUIRING_TOKEN:		 cout << "Acquiring token"; break;
			        case Allocator::CREATE_BROADCASTING_CREATE:	 cout << "Broadcasting create"; break;
			        case Allocator::CREATE_ALLOCATING_REGISTERS: cout << "Allocating registers"; break;
		        }
		        cout << endl;
            }
            cout << endl;
        }
    }
    
    {
        cout << "Cleanup queue: " << endl;
        Buffer<TID> cleanup  = alloc->GetCleanupQueue();
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
    }
    
    return true;
}

/**
 ** I-Cache functions
 **/
static bool cmd_icache_help(Object* obj, const vector<string>& /* arguments */)
{
    if (dynamic_cast<ICache*>(obj) == NULL) return false;

    cout <<
    "- info <icache-component>\n"
    "Returns information and statistics about the instruction cache.\n\n"
    "- read <icache-component>\n"
    "Reads the cache-lines of the instruction cache.\n";
    return true;
}

static bool cmd_icache_info( Object* obj, const vector<string>& /* arguments */)
{
    const ICache* cache = dynamic_cast<ICache*>(obj);
    if (cache == NULL) return false;

    size_t assoc    = cache->GetAssociativity();
    size_t nLines   = cache->GetNumLines();
    size_t lineSize = cache->GetLineSize();

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

    uint64_t hits    = cache->GetNumHits();
    uint64_t misses  = cache->GetNumMisses();
    cout << "Current hit rate:    ";
    if (hits + misses > 0) {
        cout << dec << hits * 100 / (hits + misses) << "%" << endl;
    } else {
        cout << "N/A" << endl;
    }
    return true;
}

static bool cmd_icache_read( Object* obj, const vector<string>& /* arguments */)
{
    const ICache* cache = dynamic_cast<ICache*>(obj);
    if (cache == NULL) return false;

    cout << "Set";
    for (size_t a = 0; a < cache->GetAssociativity(); ++a) {
        cout << " | Address  L  ref ";
    }
    cout << endl;
    cout << "---";
    for (size_t a = 0; a < cache->GetAssociativity(); ++a) {
        cout << "-+-----------------";
    }
    cout << endl;
    
    for (size_t s = 0; s < cache->GetNumSets(); ++s)
    {
        cout << setw(3) << setfill(' ') << dec << right << s;
        for (size_t a = 0; a < cache->GetAssociativity(); ++a)
        {
            const ICache::Line& line = cache->GetLine(s * cache->GetAssociativity() + a);
            cout << " | ";
            if (!line.used) {
                cout << "       -        ";
            } else {
                cout << right << setw(8) << setfill('0') << hex << (line.tag * cache->GetNumSets() + s) * cache->GetLineSize() << " "
                     << (line.waiting.head != INVALID_TID || line.creation ? "L" : " ") << " "
                     << "(" << setw(3) << dec << setfill(' ') << line.references << ")";
            }
        }
        cout << endl;
    }

    return true;
}

/**
 ** D-Cache functions
 **/
static bool cmd_dcache_help(Object* obj, const vector<string>& /* arguments */)
{
    if (dynamic_cast<DCache*>(obj) == NULL) return false;

    cout <<
    "- info <dcache-component>\n"
    "Returns information and statistics about the data cache.\n\n"
    "- read <dcache-component>\n"
    "Reads the cache-lines of the data cache.\n";
    return true;
}

static bool cmd_dcache_info( Object* obj, const vector<string>& /* arguments */)
{
    const DCache* cache = dynamic_cast<DCache*>(obj);
    if (cache == NULL) return false;

    size_t assoc    = cache->GetAssociativity();
    size_t nLines   = cache->GetNumLines();
    size_t lineSize = cache->GetLineSize();

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

    uint64_t hits    = cache->GetNumHits();
    uint64_t misses  = cache->GetNumMisses();
    cout << "Current hit rate:    ";
    if (hits + misses > 0) {
        cout << dec << hits * 100 / (hits + misses) << "%" << endl;
    } else {
        cout << "N/A" << endl;
    }
    cout << "Pending requests:    " << cache->GetNumPending() << endl;
    return true;
}

static bool cmd_dcache_read( Object* obj, const vector<string>& /* arguments */)
{
    const DCache* cache = dynamic_cast<DCache*>(obj);
    if (cache == NULL) return false;

	for (size_t s = 0; s < cache->GetNumSets(); ++s)
    {
        cout << left  << setfill(' ') << dec << setw(3) << s;
        cout << right << setfill('0') << hex;
        for (size_t a = 0; a < cache->GetAssociativity(); ++a)
        {
            const DCache::Line& line = cache->GetLine(s * cache->GetAssociativity() + a);
            cout << " | ";
            if (line.state == DCache::LINE_EMPTY) {
                cout << "     -    ";
            } else {
                char c;
                switch (line.state) {
                case DCache::LINE_LOADING:    c = 'L'; break;
                case DCache::LINE_PROCESSING: c = 'P'; break;
                case DCache::LINE_INVALID:    c = 'X'; break;
                default:
                case DCache::LINE_FULL:       c = ' '; break;
                }
				cout << setw(8) << (line.tag * cache->GetNumSets() + s) * cache->GetLineSize()
				     << " " << c;
            }
        }
        cout << endl;
    }

    return true;
}

/**
 ** Family Table functions
 **/
static bool cmd_families_help(Object* obj, const vector<string>& /* arguments */)
{
    if (dynamic_cast<FamilyTable*>(obj) == NULL) return false;

    cout <<
    "- read <family-table-component>\n"
    "Reads the local and global family table entries.\n";
    return true;
}

// Read the global and local family table
static bool cmd_families_read( Object* obj, const vector<string>& /* arguments */)
{
    const FamilyTable* table = dynamic_cast<FamilyTable*>(obj);
    if (table == NULL) return false;

    static const char* FamilyStates[] = {"", "ALLOCATED", "CREATE QUEUED", "CREATING", "DELEGATED", "IDLE", "ACTIVE", "KILLED"};

    cout << "    |         PC         |   Allocated    | P/N/A/Rd/Sh |  Parent | Prev | Next | State" << endl;
    cout << "----+--------------------+----------------+-------------+---------+------+------+-----------" << endl;

    cout << setfill(' ') << right;
	
	const vector<Family>& families = table->GetFamilies();
	for (size_t i = 0; i < families.size(); ++i)
    {
        const Family& family = families[i];

        cout << dec << right << setw(3) << setfill(' ') << i << " | ";
        if (family.state == FST_EMPTY)
        {
            cout << "                   |                |             |         |      |      |";
        }
        else
        {
			if (family.state == FST_ALLOCATED)
			{
                cout << "        -          |       -        |      -      | ";
			}
			else 
			{
   	            cout << hex << setw(18) << showbase << family.pc << " | " << dec;
			    if (family.state == FST_CREATING || family.state == FST_DELEGATED) {
	                cout << "      -       ";
			    } else {
	                cout << setw(3) << family.dependencies.numThreadsAllocated << "/"
					     << setw(3) << family.physBlockSize << " ("
					     << setw(4) << family.virtBlockSize << ")";
		        }
		        cout << " | ";
			    if (family.state == FST_CREATING) {
	                cout << "     -     ";
			    } else {
				    cout << !family.dependencies.prevSynchronized << "/"
				         << !family.dependencies.nextTerminated << "/"
					     << !family.dependencies.allocationDone << "/"
					     << setw(2) << family.dependencies.numPendingReads << "/"
					     << setw(2) << family.dependencies.numPendingShareds << right;
			    }
			    cout << " | ";
			}

            // Print parent
            if (family.parent.gpid != INVALID_GPID) {
                // Delegated family
				cout << setfill('0') 
				     << "F"  << setw(2) << family.parent.fid
				     << "@P" << setw(2) << family.parent.gpid;
            } else if (family.type == Family::GROUP) {
                // Group family
				cout << setfill('0') 
				     << "T"  << setw(2) << family.parent.tid
				     << "@P" << setw(2) << family.parent.lpid;
		    } else {
		        // Local family
				cout << "   -   ";
			}
			cout << " | ";

            // Print prev and next FIDs
			if (family.state == FST_ALLOCATED) {
	            cout << "  -  |  -  ";
			} else {
			    cout << setfill('0') << right;
			    if (family.link_prev != INVALID_LFID) {
				    cout << " F" << setw(2) << family.link_prev;
				} else {
				    cout << "  - ";
				}
				cout << " | ";
			    if (family.link_next != INVALID_LFID) {
				    cout << " F" << setw(2) << family.link_next;
				} else {
				    cout << "  - ";
				}
			}
            cout << " | " << FamilyStates[family.state];
        }
        cout << endl;
    }
	cout << endl << dec << table->GetNumUsedFamilies() << " used families." << endl;
	
    return true;
}

/**
 ** Thread Table functions
 **/
static bool cmd_threads_help(Object* obj, const vector<string>& /* arguments */)
{
    if (dynamic_cast<ThreadTable*>(obj) == NULL) return false;

    cout <<
    "- read <thread-table-component>\n"
    "Reads the thread table entries.\n";
    return true;
}

static bool cmd_threads_read( Object* obj, const vector<string>& /* arguments */)
{
    const ThreadTable* table = dynamic_cast<ThreadTable*>(obj);
    if (table == NULL) return false;

    static const char* ThreadStates[] = {
        "", "WAITING", "READY", "ACTIVE", "RUNNING", "SUSPENDED", "UNUSED", "KILLED"
    };

    cout << "    |         PC         | Fam | Index | Prev | Next | Int. | Flt. | Flags | WR | State" << endl;
    cout << "----+--------------------+-----+-------+------+------+------+------+-------+----+----------" << endl;
    for (TID i = 0; i < table->GetNumThreads(); ++i)
    {
        cout << dec << setw(3) << setfill(' ') << i << " | ";
        const Thread& thread = (*table)[i];

        if (thread.state != TST_EMPTY)
        {
            cout << setw(18) << setfill(' ') << hex << showbase << thread.pc << " | ";
            cout << "F" << setfill('0') << dec << noshowbase << setw(2) << thread.family << " | ";
            cout << setw(5) << dec << setfill(' ') << thread.index << " | ";
            if (thread.prevInBlock != INVALID_TID) cout << dec << setw(4) << setfill(' ') << thread.prevInBlock; else cout << "   -";
            cout << " | ";
            if (thread.nextInBlock != INVALID_TID) cout << dec << setw(4) << setfill(' ') << thread.nextInBlock; else cout << "   -";
            cout << " | ";

            for (RegType type = 0; type < NUM_REG_TYPES; ++type)
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
            cout << "                   |     |       |      |      |      |      |       |    |";
        }
        cout << endl;
    }
	cout << endl << dec << table->GetNumUsedThreads() << " used threads." << endl;
    return true;
}

/**
 ** Pipeline functions
 **/
static bool cmd_pipeline_help(Object* obj, const vector<string>& /* arguments */)
{
    if (dynamic_cast<Pipeline*>(obj) == NULL) return false;

    cout <<
    "- read <pipeline-component>\n"
    "Reads the contents of the pipeline latches.\n";
    return true;
}

// Construct a string representation of a pipeline register value
static string MakePipeValue(const RegType& type, const PipeValue& value)
{
    stringstream ss;

    switch (value.m_state)
    {
        case RST_INVALID:   ss << "N/A";   break;
        case RST_EMPTY:     ss << "Empty"; break;
		case RST_WAITING:   ss << "Waiting (" << setw(4) << setfill('0') << value.m_waiting.head << "h)"; break;
        case RST_FULL:
            if (type == RT_INTEGER) {
                ss << setw(value.m_size * 2);
                ss << setfill('0') << hex << value.m_integer.get(value.m_size) << "h";
            } else {
                ss << setprecision(16) << fixed << value.m_float.tofloat(value.m_size);
            }
            break;
    }
	
	string ret = ss.str();
	if (ret.length() > 17) {
		ret = ret.substr(0,17);
	}
    return ret;
}

static ostream& operator<<(ostream& out, const RemoteRegAddr& rreg) {
    if (rreg.fid != INVALID_LFID) {
        out << hex << setw(2) << setfill('0') << rreg.reg.str() << ", F" << dec << rreg.fid;
        if (rreg.pid != INVALID_GPID) {
            out << "@P" << rreg.pid;
        }
    } else {
        out << "N/A";
    }
    return out;
}

// Read the pipeline latches
static bool cmd_pipeline_read( Object* obj, const vector<string>& /* arguments */)
{
    const Pipeline* pipe = dynamic_cast<Pipeline*>(obj);
    if (pipe == NULL) return false;

    // Get the stages and latches
    const Pipeline::FetchStage&       fetch   = (const Pipeline::FetchStage&)    pipe->GetStage(0);
    const Pipeline::DecodeStage&      decode  = (const Pipeline::DecodeStage&)   pipe->GetStage(1);
    const Pipeline::ReadStage&        read    = (const Pipeline::ReadStage&)     pipe->GetStage(2);
    const Pipeline::ExecuteStage&     execute = (const Pipeline::ExecuteStage&)  pipe->GetStage(3);
    const Pipeline::MemoryStage&      memory  = (const Pipeline::MemoryStage&)   pipe->GetStage(4);
    const Pipeline::FetchDecodeLatch&     fdlatch = *(const Pipeline::FetchDecodeLatch*)    fetch  .getOutput();
    const Pipeline::DecodeReadLatch&      drlatch = *(const Pipeline::DecodeReadLatch*)     decode .getOutput();
    const Pipeline::ReadExecuteLatch&     relatch = *(const Pipeline::ReadExecuteLatch*)    read   .getOutput();
    const Pipeline::ExecuteMemoryLatch&   emlatch = *(const Pipeline::ExecuteMemoryLatch*)  execute.getOutput();
    const Pipeline::MemoryWritebackLatch& mwlatch = *(const Pipeline::MemoryWritebackLatch*)memory .getOutput();

    // Fetch stage
    cout << "Fetch stage" << endl;
    cout << " |" << endl;
    if (fdlatch.empty)
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
    if (drlatch.empty)
    {
        cout << " | (Empty)" << endl;
    }
    else
    {
        cout << " | LFID: "  << dec << drlatch.fid
             << "    TID: "  << dec << drlatch.tid
             << "    PC: "   << hex << setw(8) << setfill('0') << drlatch.pc << "h"
             << "    Annotation: " << ((drlatch.kill) ? "End" : (drlatch.swch ? "Switch" : "None")) << endl 
             << " |" << endl
             << hex << setfill('0')
#if TARGET_ARCH == ARCH_ALPHA
             << " | Opcode:       " << setw(2) << (int)drlatch.opcode << "h" << endl
             << " | Function:     " << setw(4) << drlatch.function << "h" << endl
             << " | Displacement: " << setw(8) << drlatch.displacement << "h" << endl
#elif TARGET_ARCH == ARCH_SPARC
             << " | Op1:          " << setw(2) << (int)drlatch.op1 << "h"
             << "    Op2: " << setw(2) << (int)drlatch.op2 << "h"
             << "    Op3: " << setw(2) << (int)drlatch.op3 << "h" << endl
             << " | Function:     " << setw(4) << drlatch.function << "h" << endl
             << " | Displacement: " << setw(8) << drlatch.displacement << "h" << endl
#endif
             << " | Literal:      " << setw(8) << drlatch.literal << "h" << endl
             << dec
             << " | Ra:           " << drlatch.Ra << "/" << drlatch.RaSize << "    Rra: " << drlatch.Rra << endl
             << " | Rb:           " << drlatch.Rb << "/" << drlatch.RbSize << "    Rrb: " << drlatch.Rrb << endl
             << " | Rc:           " << drlatch.Rc << "/" << drlatch.RcSize << "    Rrc: " << drlatch.Rrc << endl;
    }
    cout << " v" << endl;

    // Read stage
    cout << "Read stage" << endl;
    cout << " |" << endl;
    if (relatch.empty)
    {
        cout << " | (Empty)" << endl;
    }
    else
    {
        cout << " | LFID: "  << dec << relatch.fid
             << "    TID: "  << dec << relatch.tid
             << "    PC: "   << hex << setw(8) << setfill('0') << relatch.pc << "h"
             << "    Annotation: " << ((relatch.kill) ? "End" : (relatch.swch ? "Switch" : "None")) << endl 
             << " |" << endl
             << hex << setfill('0')
#if TARGET_ARCH == ARCH_ALPHA
             << " | Opcode:       " << setw(2) << (int)relatch.opcode << "h" << endl
             << " | Function:     " << setw(4) << relatch.function << "h" << endl
             << " | Displacement: " << setw(8) << relatch.displacement << "h" << endl
#elif TARGET_ARCH == ARCH_SPARC
             << " | Op1:          " << setw(2) << (int)drlatch.op1 << "h"
             << "    Op2: " << setw(2) << (int)drlatch.op2 << "h"
             << "    Op3: " << setw(2) << (int)drlatch.op3 << "h" << endl
             << " | Function:     " << setw(4) << relatch.function << "h" << endl
             << " | Displacement: " << setw(8) << relatch.displacement << "h" << endl
#endif
             << " | Rav:          " << MakePipeValue(relatch.Ra.type, relatch.Rav) << "/" << relatch.Rav.m_size << endl
             << " | Rbv:          " << MakePipeValue(relatch.Rb.type, relatch.Rbv) << "/" << relatch.Rbv.m_size << endl
             << " | Rra:          " << relatch.Rra << endl
             << " | Rrb:          " << relatch.Rrb << endl
             << dec
             << " | Rc:           " << relatch.Rc << "/" << relatch.Rcv.m_size << "    Rrc: " << relatch.Rrc << endl;
    }
    cout << " v" << endl;

    // Execute stage
    cout << "Execute stage" << endl;
    cout << " |" << endl;
    if (emlatch.empty)
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
        cout << " | Rc:        " << emlatch.Rc << "/" << emlatch.Rcv.m_size << "    Rrc: " << emlatch.Rrc << endl;
        cout << " | Rcv:       " << MakePipeValue(emlatch.Rc.type, emlatch.Rcv) << endl;
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
            cout << " | Address:   " << hex << setw(sizeof(MemAddr) * 2) << setfill('0') << emlatch.address << "h" << endl;
            cout << " | Size:      " << hex << setw(4) << setfill('0') << emlatch.size    << "h" << endl;
        }
    }
    cout << " v" << endl;
    
    // Memory stage
    cout << "Memory stage" << endl;
    cout << " |" << endl;
    if (mwlatch.empty)
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
        cout << " | Rc:   " << mwlatch.Rc << "/" << mwlatch.Rcv.m_size << "    Rrc: " << mwlatch.Rrc << endl;
        cout << " | Rcv:  " << MakePipeValue(mwlatch.Rc.type, mwlatch.Rcv) << endl;
    }
    cout << " v" << endl;

    // Writeback stage
    cout << "Writeback stage" << endl;
//#else
//    cout << "Sorry, this architecture's pipeline printing has not been implemented yet" << endl;
    return true;
}

/**
 ** RAU functions
 **/
static bool cmd_rau_help(Object* obj, const vector<string>& /* arguments */)
{
    if (dynamic_cast<RAUnit*>(obj) == NULL) return false;

    cout <<
    "- read <raunit-component>\n"
    "Reads the allocation of registers from the register allocation unit.\n";
    return true;
}

static const char* TypeNames[NUM_REG_TYPES] = {"Integer", "Float"};
static const char* StateNames[5] = {"", "Empty", "Waiting", "Full"};

// Read the Register Allocation Unit
static bool cmd_rau_read( Object* obj, const vector<string>& arguments )
{
    const RAUnit* rau = dynamic_cast<RAUnit*>(obj);
    if (rau == NULL) return false;

    RegType type = (arguments.size() > 0 && arguments[0] == "float") ? RT_FLOAT : RT_INTEGER;
    const RAUnit::List& list = rau->GetList(type);
    const RegSize blockSize  = rau->GetBlockSize(type);

    cout << TypeNames[type] << " registers:" << endl;
    for (size_t next, entry = 0; entry < list.size(); entry = next)
    {
        cout << hex << setw(4) << entry * blockSize << "h - " << setw(4);

        if (list[entry].first != 0) {
            next = entry + list[entry].first;
            cout << (next * blockSize) - 1 << "h: Allocated to " << list[entry].second << endl;
        } else {
            for (next = entry + 1; next < list.size() && list[next].first == 0; ++next) {}
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
    RegClass group;

    REGINFO() : fid(INVALID_LFID), tid(INVALID_TID) {}
};

/**
 ** RegisterFile functions
 **/
static bool cmd_regs_help(Object* obj, const vector<string>& /* arguments */)
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
    const Object* proc = regfile->GetParent();
    if (proc != NULL)
    {
        for (unsigned int i = 0; i < proc->GetNumChildren(); ++i)
        {
            const Object* child = proc->GetChild(i);
            if (rau    == NULL) rau    = dynamic_cast<const RAUnit*>(child);
            if (ftable == NULL) ftable = dynamic_cast<const FamilyTable*>(child);
            if (alloc  == NULL) alloc  = dynamic_cast<const Allocator*>(child);
        }
    }

    RegType type = (arguments.size() > 0 && arguments[0] == "float") ? RT_FLOAT : RT_INTEGER;
    RegSize size = regfile->GetSize(type);

    vector<REGINFO> regs(size);
    if (rau != NULL)
    {
        const RAUnit::List& list = rau->GetList(type);
        const RegSize blockSize  = rau->GetBlockSize(type);
        for (size_t i = 0; i < list.size();)
        {
            if (list[i].first != 0)
            {
                for (size_t j = 0; j < list[i].first * blockSize; ++j)
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
        for (size_t i = 0; i < regs.size(); ++i)
        {
            if (regs[i].fid != INVALID_LFID)
            {
                const Family& family = (*ftable)[regs[i].fid];
                for (TID t = 0; t < family.physBlockSize; ++t)
                {
                }
            }
        }
    }

    cout << "      |  State  | MR |       Value      | Fam | Thread | Type"       << endl;
    cout << "------+---------+----+------------------+-----+--------+-----------" << endl;
    for (RegIndex i = size; i > 0; i--)
    {
        RegValue value;
        RegAddr  addr = MAKE_REGADDR(type, i - 1);
        LFID      fid = regs[i - 1].fid;
        regfile->ReadRegister(addr, value);
        cout << addr << " | " << setw(7) << setfill(' ') << StateNames[value.m_state] << " | ";
        if (value.m_state != RST_FULL)
        {
            cout << (value.m_memory.size != 0            ? 'M' : ' ');
            cout << (value.m_remote.fid  != INVALID_LFID ? 'R' : ' ');
        }
        else
        {
            cout << "  ";
        }
        cout << " | ";
        
		stringstream ss;
        switch (value.m_state)
        {
        case RST_FULL:
            switch (type)
            {
            case RT_INTEGER: ss << setw(16 - sizeof(Integer) * 2) << setfill(' ') << ""
                                << setw(     sizeof(Integer) * 2) << setfill('0') << hex << value.m_integer; break;
            case RT_FLOAT:   ss << setw(16 - sizeof(Integer) * 2) << setfill(' ') << ""
                                << setw(     sizeof(Integer) * 2) << setfill('0') << hex << value.m_float.integer; break;
            }
            break;

        case RST_WAITING:
            ss << "   " << setw(4) << setfill('0') << hex << value.m_waiting.head << " - " << setw(4) << value.m_waiting.tail << "  "; break;
            break;

        case RST_INVALID:
        case RST_EMPTY:
            ss << setw(16) << " ";
            break;
        }

		cout << ss.str().substr(0, 16) << " | ";
        if (fid != INVALID_LFID) cout << "F" << setw(2) << setfill('0') << dec << fid; else cout << "   ";
        cout << " |  ";

		RegClass group = RC_LOCAL;
		TID      tid   = (fid != INVALID_LFID) ? alloc->GetRegisterType(fid, addr, &group) : INVALID_TID;
		if (tid != INVALID_TID) {
			cout << "T" << setw(4) << setfill('0') << tid;
		} else {
			cout << "  -  ";
		}
		cout << " | ";
		switch (group)
		{
			case RC_GLOBAL:    cout << "Global"; break;
			case RC_DEPENDENT: cout << "Dependent"; break;
			case RC_SHARED:    cout << "Shared"; break;
			case RC_LOCAL:     
				if (tid != INVALID_TID) cout << "Local";
				break;
		    case RC_RAZ: break;
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
    {NULL, NULL}
};
