#ifndef _NETWORK_PORT_H
#define _NETWORK_PORT_H

#include "predef.h"

#include "network_if.h"
#include "network_node.h"

#include "networkbelow_if.h"
#include "networkabove_if.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class Network_port
	: public sc_port<Network_if, 0>, public SimObj
{
	// typdefs

public:

protected:
	sc_in_clk *pport_clk;
	
	vector<Network_Node*> m_vecLpNode;

public:
	
	// constructors
	Network_port()
//		: sc_port( 0 )
	{
	}

	void operator () ( NetworkBelow_if& interface_ )
	{
		// bind interface with network port
		sc_port_base::bind(interface_);

		//-- add new node with appropriate ID --
		// make new name
		char sname[100];
		sprintf(sname, "%s_Node%02d", this->name(), m_vecLpNode.size());
		validatename(sname);
		Network_Node* newNode = new Network_Node(sname, interface_.m_fifoinNetwork);
		m_vecLpNode.push_back(newNode);

		// bind new node with the interface thing (a level 2 cache), and keep a list
		//newNode->m_fifoOut(interface_.m_fifoinNetwork);
		interface_.m_fifooutNetwork(newNode->m_fifoIn);
		newNode->port_forward(newNode->signal_forward);
		interface_.port_net_forward(newNode->signal_forward);

		// set interface->m_oNode = the new node;
		newNode->port_clk(*pport_clk);
		newNode->SetCache(&interface_);

	}

	void operator () ( NetworkAbove_if& interface_ )
	{
		// bind interface with network port
		sc_port_base::bind(interface_);

		//-- add new node with appropriate ID --
		// make new name
		char sname[100];
		sprintf(sname, "%s_Node%02d", this->name(), m_vecLpNode.size());
		validatename(sname);
		Network_Node* newNode = new Network_Node(sname, interface_.m_fifoinNetwork);
		m_vecLpNode.push_back(newNode);

		// bind new node with the interface thing (a level 2 cache), and keep a list
		//newNode->m_fifoOut(interface_.m_fifoinNetwork);
		interface_.m_fifooutNetwork(newNode->m_fifoIn);
		newNode->port_forward(newNode->signal_forward);
		interface_.port_net_forward(newNode->signal_forward);

		// set interface->m_oNode = the new node;
		newNode->port_clk(*pport_clk);
		newNode->SetCache(&interface_);
	}

	void SetClockPort(sc_in_clk *clk){pport_clk = clk;};

	void ConnectNodes();

    virtual void InitializeLog(const char* logName, LOG_LOCATION ll, VERBOSE_LEVEL verbose = VERBOSE_ALL )
    {
        SimObj::InitializeLog(logName, ll, verbose);

        for (unsigned int i=0;i<m_vecLpNode.size();i++)
        {
            Network_Node* pNode = m_vecLpNode.at(i);
            pNode->InitializeLog(logName, ll, verbose);
        }
    }

};

//////////////////////////////
//} memory simulator namespace
}

#endif
