#ifndef _TOPS_H
#define _TOPS_H

#include "predef.h"

#include "memorys.h"
#include "cachel1s.h"
#include "processors.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class TopS : public sc_module
{
private:
	bool	m_bVerbose;
	
public:
	sc_clock *clk;
	ProcessorS *proc;
	CacheL1S *cachel1;
	MemoryS *mem;

	TopS(sc_module_name nm) : sc_module(nm)
	{
		clk = new sc_clock("clk", 10, SC_NS);
		//sc_clock clk("clk", 10, SC_NS);

		sc_signal<__address_t> *address1, *address2;
		sc_signal<__address_t> *data1, *data2;

		sc_signal<MemoryS::STATE> *state1, *state2;
		sc_signal<MemoryS::SIGNAL> *signal1, *signal2;

		address1 = new sc_signal<__address_t>();
		address2 = new sc_signal<__address_t>();
		data1 = new sc_signal<__address_t>();
		data2 = new sc_signal<__address_t>();

		state1 = new sc_signal<MemoryS::STATE>();
		state2 = new sc_signal<MemoryS::STATE>();
		signal1 = new sc_signal<MemoryS::SIGNAL>();
		signal2 = new sc_signal<MemoryS::SIGNAL>();


		//ProcessorS proc("Processor", 70, 30, 0);
		proc = new ProcessorS("Processor");
		proc->port_clk(*clk);
		proc->port_address(*address1);
		proc->port_data(*data1);
		proc->port_mem_signal(*signal1);
		proc->port_mem_state(*state1);

		cachel1 = new CacheL1S("Cache", 128, 2, 128, CacheL1S::RP_LRU);
		cachel1->port_cpu_address(*address1);
		cachel1->port_cpu_data(*data1);
		cachel1->port_cpu_signal(*signal1);
		cachel1->port_cpu_state(*state1);
		cachel1->port_mem_address(*address2);
		cachel1->port_mem_data(*data2);
		cachel1->port_mem_signal(*signal2);
		cachel1->port_mem_state(*state2);

		mem = new MemoryS("Memory");
		mem->port_address(*address2);
		mem->port_data(*data2);
		mem->port_signal(*signal2);
		mem->port_state(*state2);

		//MemoryS mem("Memory");
		//mem.port_address(*address1);
		//mem.port_data(*data1);
		//mem.port_signal(*signal1);
		//mem.port_state(*state1);

		m_bVerbose = true;
	}

	void SetVerbose(bool bVerbose){m_bVerbose = bVerbose;}
};

//////////////////////////////
//} memory simulator namespace
}

#endif
