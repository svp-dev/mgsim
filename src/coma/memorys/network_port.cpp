#include "network_port.h"
using namespace MemSim;


void Network_port::ConnectNodes()
{
	int size = m_vecLpNode.size();
	if (size <= 1)
	{
		cerr << ERR_HEAD_OUTPUT << "Network with less than 2 nodes!" << endl;
		exit(-1);
	}
	
	Network_Node* pNodePre = m_vecLpNode.at(0);
	Network_Node* pNode;
	for (int i=1;i<size;i++)
	{
		pNode = m_vecLpNode.at(i);
		pNodePre->m_fifoNetOut(pNode->m_fifoNetIn);
        clog << "connecting " << pNode->name() << endl;

		pNodePre = pNode;
	}

	pNode->m_fifoNetOut((m_vecLpNode.at(0))->m_fifoNetIn);


}

