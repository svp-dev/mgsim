#include <iostream>
#include <sstream>

#include <string.h>
#include <cstring>
#include <vector>
#include <tinyxml/tinyxml.h>
using namespace std;

#include "ddrxml.h"


const char* STR_ELE_DDR_INTERFACES = "DDR_Interfaces";
const char* STR_ELE_INTERFACE = "interface";
const char* STR_ELE_CHANNEL = "channel";
const char* STR_ELE_TIMING = "timing";
const char* STR_ELE_DEVICE = "device";
const char* STR_ELE_CONFIG = "config";
const char* STR_ELE_DDR_CONFIGURATIONS = "DDR_Configurations";
const char* STR_ELE_CONFIGURATION = "configuration";

const char* STR_ATT_ID = "id";
const char* STR_ATT_NAME = "name";

const char* STR_ATT_NUMBER = "number";
const char* STR_ATT_MODE = "mode";

const char* STR_ATT_TAL = "tAL";
const char* STR_ATT_TCL = "tCL";
const char* STR_ATT_TCWL = "tCWL";
const char* STR_ATT_TRCD = "tRCD";
const char* STR_ATT_TRP = "tRP";
const char* STR_ATT_TRAS = "tRAS";

const char* STR_ATT_BANKS = "Banks";
const char* STR_ATT_ROWBITS = "RowBits";
const char* STR_ATT_COLUMNBITS = "ColumnBits";
const char* STR_ATT_CELLSIZE = "CellSize";
const char* STR_ATT_DATAPATH = "DataPath";
const char* STR_ATT_BURSTLENGTH = "BurstLength";

const char* STR_ATT_RANKS= "Ranks";
const char* STR_ATT_DEVICEPERRANK = "DevicePerRank";

#include "predef.h"
using MemSim::lg2;

bool parseconfig(TiXmlNode* proot, ddr_interface* pinterface)
{
    TiXmlNode* pNode = 0;

    pinterface->nRankBits = 0;
    pinterface->nDevicePerRank = 0;

    while ( (pNode = proot->IterateChildren(pNode)) )
    {
        TiXmlElement* pElem = dynamic_cast<TiXmlElement*>(pNode);
        if (!pElem)
            continue;

        if (strcmp(pElem->Value(), STR_ELE_CONFIG) == 0)
        {
            int ranks;
            if (pElem->Attribute(STR_ATT_RANKS, &ranks) && pElem->Attribute(STR_ATT_DEVICEPERRANK, &pinterface->nDevicePerRank))
            {
                pinterface->nRankBits = lg2(ranks);

//                cout << pNode->Value() << " : " << pinterface->nRankBits << " . " << pinterface->nDevicePerRank << endl;

                return true;
            }
            else
                return false;
        }
        else   
            continue;
    }

    return false;
}

bool parsedevice(TiXmlNode* proot, ddr_interface* pinterface)
{
    TiXmlNode* pNode = 0;

    pinterface->nBankBits = 0;
    pinterface->nRowBits = 0;
    pinterface->nColumnBits = 0;
    pinterface->nCellSizeBits = 0;
    pinterface->nDataPathBits = 0;
    pinterface->nBurstLength = 0;

    while ( (pNode = proot->IterateChildren(pNode)) )
    {
        TiXmlElement* pElem = dynamic_cast<TiXmlElement*>(pNode);
        if (!pElem)
            continue;

        if (strcmp(pElem->Value(), STR_ELE_DEVICE) == 0)
        {
            int banks, rowbits, columnbits, cellsize, datapath, burstlength;
            if (pElem->Attribute(STR_ATT_BANKS, &banks) && pElem->Attribute(STR_ATT_ROWBITS, &rowbits) && pElem->Attribute(STR_ATT_COLUMNBITS, &columnbits) && pElem->Attribute(STR_ATT_CELLSIZE, &cellsize) && pElem->Attribute(STR_ATT_DATAPATH, &datapath) && pElem->Attribute(STR_ATT_BURSTLENGTH, &burstlength))
            {
                pinterface->nBankBits = lg2(banks);
                pinterface->nRowBits = rowbits;
                pinterface->nColumnBits = columnbits;
                pinterface->nCellSizeBits = lg2(cellsize);
                pinterface->nDataPathBits = datapath;
                pinterface->nBurstLength = burstlength;

//                cout << pNode->Value() << " : " << pinterface->nBankBits << " . " << pinterface->nRowBits
//                    << " . " << pinterface->nColumnBits << " . " << pinterface->nCellSizeBits 
//                    << " . " << pinterface->nDataPathBits << " . " << pinterface->nBurstLength << endl;

                return true;
            }
            else
                return false;
        }
        else   
            continue;
    }

    return false;
}

bool parsetiming(TiXmlNode* proot, ddr_interface* pinterface)
{
    TiXmlNode* pNode = 0;

    pinterface->tAL = 0;
    pinterface->tCL = 0;
    pinterface->tCWL = 0;
    pinterface->tRCD = 0;
    pinterface->tRP = 0;
    pinterface->tRAS = 0;

    while ( (pNode = proot->IterateChildren(pNode)) )
    {
        TiXmlElement* pElem = dynamic_cast<TiXmlElement*>(pNode);
        if (!pElem)
            continue;


        if (strcmp(pElem->Value(), STR_ELE_TIMING) == 0)
        {
            if (pElem->Attribute(STR_ATT_TAL, &pinterface->tAL) && pElem->Attribute(STR_ATT_TCL, &pinterface->tCL) && pElem->Attribute(STR_ATT_TCWL, &pinterface->tCWL) && pElem->Attribute(STR_ATT_TRCD, &pinterface->tRCD) && pElem->Attribute(STR_ATT_TRP, &pinterface->tRP) && pElem->Attribute(STR_ATT_TRAS, &pinterface->tRAS))
            {
//                cout << pNode->Value() << " : " << pinterface->tAL << " . " << pinterface->tCL 
//                    << " . " << pinterface->tCWL << " . " << pinterface->tRCD 
//                    << " . " << pinterface->tRP << " . " << pinterface->tRAS << endl;

                return true;
            }
            else
                return false;
        }
        else   
            continue;
    }

    return false;
}

bool parsechannel(TiXmlNode* proot, ddr_interface* pinterface)
{
    pinterface->nChannel = 0;
    pinterface->nModeChannel = 0;

    TiXmlNode* pNode = 0;

    while ( (pNode = proot->IterateChildren(pNode)) )
    {
        TiXmlElement* pElem = dynamic_cast<TiXmlElement*>(pNode);
        if (!pElem)
            continue;

        if (strcmp(pElem->Value(), STR_ELE_CHANNEL) == 0)
        {
            if (pElem->Attribute(STR_ATT_NUMBER, &pinterface->nChannel) && pElem->Attribute(STR_ATT_MODE, &pinterface->nModeChannel))
            {
//                cout << pNode->Value() << " : " << pinterface->nChannel << " . " << pinterface->nModeChannel << endl;
                return true;
            }
            else
                return false;
        }
        else   
            continue;
    }

    return false;
}

// return number of ddr_interface detected
int parseinterfaces(TiXmlNode* proot, map<int, ddr_interface*>* &pinterfaces)
{
    map<int, ddr_interface*>* pmapddr = new map<int, ddr_interface*>();

    TiXmlNode* pNode = 0;
    while ( (pNode = proot->IterateChildren(pNode)) )
    {
        TiXmlElement* pElem = dynamic_cast<TiXmlElement*>(pNode);
        if (!pElem)
            continue;

        if (strcmp(pNode->Value(), STR_ELE_INTERFACE) == 0)
        {
            int id;
            const char* pname = NULL;
            if (!pElem->Attribute(STR_ATT_ID, &id))
                continue;

            pname = pElem->Attribute(STR_ATT_NAME);
            if (pname == NULL)
                continue;

//            cout << "id " << id << " name " << pname << endl;
            ddr_interface* pddr = new ddr_interface();

            pddr->id = id;
            pddr->name = pname;

            parsechannel(pNode, pddr);
            parsetiming(pNode, pddr);
            parseconfig(pNode, pddr);
            parsedevice(pNode, pddr);

            pmapddr->insert(pair<int, ddr_interface*>(id, pddr));
        }
    }

    if (pmapddr->empty())
    {
        delete pmapddr;
        return 0;
    }
    else
        pinterfaces = pmapddr;

    return pmapddr->size();
}

map<int, ddr_interface*>* loadxmlddrinterfaces(const char* fname)
{
    TiXmlDocument doc(fname);
    if (!doc.LoadFile()) return NULL;

    TiXmlHandle hDoc(&doc);
    TiXmlNode* pNode;
    TiXmlHandle hRoot(0);
    TiXmlHandle hInterfaces(0);

    pNode=0;

    map<int, ddr_interface*>* pinterfaces = NULL;

    while ( (pNode = doc.IterateChildren(pNode)) )
    {
        if (strcmp(pNode->Value(), STR_ELE_DDR_INTERFACES) == 0)
        {
            parseinterfaces(pNode, pinterfaces);
        }
    }

    return pinterfaces;
}


// return number of configurations detected
int parseconfigurations(map<int, ddr_interface*>* pinterfaces, TiXmlNode* proot, map<int, vector<int> >* &pconfs)
{
    map<int, vector<int> >* pmapconf = new map<int, vector<int> >();

    TiXmlNode* pNode = 0;
    while ( (pNode = proot->IterateChildren(pNode)) )
    {
        TiXmlElement* pElem = dynamic_cast<TiXmlElement*>(pNode);
        if (!pElem)
            continue;

        if (strcmp(pNode->Value(), STR_ELE_CONFIGURATION) == 0)
        {
            int id;
            if (!pElem->Attribute(STR_ATT_ID, &id))
                continue;

            TiXmlNode* pint = 0;
            vector<int> conf;
            while( (pint = pNode->IterateChildren(pint)) )
            {
                int intid;
                if (!strcmp(pint->Value(), STR_ELE_INTERFACE) == 0)
                {
                    continue;
                }

                if (!(dynamic_cast<TiXmlElement*>(pint))->Attribute(STR_ATT_ID, &intid))
                {
                    continue;
                }


                if (pinterfaces->find(intid) == pinterfaces->end())
                {
                    continue;
                }

                conf.push_back(intid);
            }

            if (conf.size() > 0)
            {
                pair<int, vector<int> > pairconf;
                pairconf.first = id;
                pairconf.second = conf;
                pmapconf->insert(pairconf);
            }
        }
    }

    if (pmapconf->empty())
    {
        delete pmapconf;
        return 0;
    }
    else
        pconfs = pmapconf;

    return pmapconf->size();
}


map<int, vector<int> >* loadxmlddrconfigurations(map<int, ddr_interface*>* pinterfaces, const char* fname)
{
    if (pinterfaces == NULL)
        return NULL;

    TiXmlDocument doc(fname);
    if (!doc.LoadFile()) return NULL;

    TiXmlHandle hDoc(&doc);
    TiXmlNode* pNode;
    TiXmlHandle hRoot(0);
    TiXmlHandle hInterfaces(0);

    pNode=0;

    map<int, vector<int> >* pret = NULL;

    while ( (pNode = doc.IterateChildren(pNode)) )
    {
        if (strcmp(pNode->Value(), STR_ELE_DDR_CONFIGURATIONS) == 0)
        {
            parseconfigurations(pinterfaces, pNode, pret);
        }
    }

    return pret;

}



