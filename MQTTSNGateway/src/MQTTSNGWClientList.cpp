/**************************************************************************************
 * Copyright (c) 2016, Tomoaki Yamaguchi
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Tomoaki Yamaguchi - initial API and implementation and/or initial documentation
 *    Tieto Poland Sp. z o.o. - Gateway improvements
 **************************************************************************************/
#include "MQTTSNGWClientList.h"
#include "MQTTSNGateway.h"
#include <string.h>
#include <string>

using namespace MQTTSNGW;
extern Gateway* theGateway;
/*=====================================
 Class ClientList
 =====================================*/
const char* common_topic = "*";

ClientList::ClientList()
{
    _clientCnt = 0;
    _authorize = false;
    _firstClient = nullptr;
    _endClient = nullptr;
}

ClientList::~ClientList()
{
    _mutex.lock(15);
    Client* cl = _firstClient;
    Client* ncl;

    while (cl != nullptr)
    {
        ncl = cl->_nextClient;
        delete cl;
        cl = ncl;
    };
    _mutex.unlock(15);
}

void ClientList::initialize(bool aggregate)
{
    if (theGateway->getGWParams()->clientAuthentication )
    {
		int type = TRANSPEARENT_TYPE;
		if ( aggregate )
		{
			type = AGGREGATER_TYPE;
		}
		setClientList(type);
        _authorize = true;
    }

    if ( theGateway->getGWParams()->predefinedTopic )
    {
    	setPredefinedTopics(aggregate);
    }
}

void ClientList::setClientList(int type)
{
	if (!createList(theGateway->getGWParams()->clientListName, type))
	{
		throw Exception("ClientList::setClientList No client list defined by config file.");
	}
}

void ClientList::setPredefinedTopics(bool aggrecate)
{
	if ( !readPredefinedList(theGateway->getGWParams()->predefinedTopicFileName, aggrecate) )
	{
		throw Exception("ClientList::setPredefinedTopics No predefindTopi list defined by config file.");

	}
}

/**
 * Create ClientList from a client list file.
 * @param File name of the client list
 * @return true: Reject client connection that is not registered in the client list
 *
 * File format is:
 *     Lines bigning with # are comment line.
 *     ClientId, SensorNetAddress, "unstableLine", "secureConnection"
 *     in case of UDP, SensorNetAddress format is IPAddress:portNo.
 *     if the SensorNetwork is not stable, write unstableLine.
 *     if BrokerConnection is SSL, write secureConnection.
 *     if the client send PUBLISH QoS-1, QoS-1 is required.
 *
 * Ex:
 *     #Client List
 *     ClientId1,192.168.10.10:11200
 *     ClientID2,192.168.50.200:35000,unstableLine
 *     ClientID3,192.168.200.50:40000,secureConnection
 *     ClientID4,192.168.200.51:41000,unstableLine,secureConnection
 *     ClientID5,192.168.200.51:41000,unstableLine,secureConnection,QoS-1
 */

bool ClientList::createList(const char* fileName, int type)
{
    FILE* fp;
    char buf[MAX_CLIENTID_LENGTH + 256];
    size_t pos;
    bool secure;
    bool stable;
    bool qos_1;
    bool forwarder;
    bool rc = false;
    SensorNetAddress netAddr;
    MQTTSNString clientId = MQTTSNString_initializer;

    if ((fp = fopen(fileName, "r")) != 0)
    {
        while (fgets(buf, MAX_CLIENTID_LENGTH + 254, fp) != 0)
        {
            if (*buf == '#')
            {
                continue;
            }
            string data = string(buf);
            while ((pos = data.find_first_of(" 　\t\n")) != string::npos)
            {
                data.erase(pos, 1);
            }
            if (data.empty())
            {
                continue;
            }
            pos = data.find_first_of(",");
            string id = data.substr(0, pos);
            clientId.cstring = strdup(id.c_str());
            string addr = data.substr(pos + 1);

            if (netAddr.setAddress(&addr) == 0)
            {
                qos_1 = (data.find("QoS-1") != string::npos);
                forwarder = (data.find("forwarder") != string::npos);
                secure = (data.find("secureConnection") != string::npos);
                stable = !(data.find("unstableLine") != string::npos);
                if ( (qos_1 && type == QOSM1PROXY_TYPE) || (!qos_1 && type == AGGREGATER_TYPE) )
                {
                	createClient(&netAddr, &clientId, stable, secure, type);
                }
                else if ( forwarder && type == FORWARDER_TYPE)
                {
                	theGateway->getAdapterManager()->getForwarderList()->addForwarder(&netAddr, &clientId);
                }
                else if (type == TRANSPEARENT_TYPE )
                {
                	createClient(&netAddr, &clientId, stable, secure, type);
                }
            }
            else
            {
                WRITELOG("Invalid address     %s\n", data.c_str());
                rc = false;
            }
            free(clientId.cstring);
        }
        fclose(fp);
        rc = true;
    }
    return rc;
}

bool ClientList::readPredefinedList(const char* fileName, bool aggregate)
{
    FILE* fp;
    char buf[MAX_CLIENTID_LENGTH + 256];
    size_t pos0, pos1;
    MQTTSNString clientId = MQTTSNString_initializer;;
    bool rc = false;

    if ((fp = fopen(fileName, "r")) != 0)
    {
        while (fgets(buf, MAX_CLIENTID_LENGTH + 254, fp) != 0)
        {
            if (*buf == '#')
            {
                continue;
            }
            string data = string(buf);
            while ((pos0 = data.find_first_of(" 　\t\n")) != string::npos)
            {
                data.erase(pos0, 1);
            }
            if (data.empty())
            {
                continue;
            }

            pos0 = data.find_first_of(",");
            pos1 = data.find(",", pos0 + 1) ;
            string id = data.substr(0, pos0);
            clientId.cstring = strdup(id.c_str());
            string topicName = data.substr(pos0 + 1, pos1 - pos0 -1);
            uint16_t topicID = stoul(data.substr(pos1 + 1));
            createPredefinedTopic( &clientId, topicName,  topicID, aggregate);
            free(clientId.cstring);
        }
        fclose(fp);
        rc = true;
    }
    else
    {
        WRITELOG("ClientList can not open the Predefined Topic List.     %s\n", fileName);
        return false;
    }
    return rc;
}

void ClientList::erase(Client*& client)
{
    if ( !_authorize && client->erasable())
    {
        _mutex.lock(16);
        Client* prev = client->_prevClient;
        Client* next = client->_nextClient;

        if (prev)
        {
            prev->_nextClient = next;
        }
        else
        {
            _firstClient = next;

        }
        if (next)
        {
            next->_prevClient = prev;
        }
        else
        {
            _endClient = prev;
        }
        _clientCnt--;
        Forwarder* fwd = client->getForwarder();
        if ( fwd )
        {
            fwd->eraseClient(client);
        }
        delete client;
        client = nullptr;
        _mutex.unlock(16);
    }
}

Client* ClientList::getClient(SensorNetAddress* addr)
{
    if ( addr )
    {
        _mutex.lock(17);
        Client* client = _firstClient;

        while (client != nullptr)
        {
            if (client->getSensorNetAddress()->isMatch(addr) )
            {
                _mutex.unlock(17);
                return client;
            }
            client = client->_nextClient;
        }
        _mutex.unlock(17);
    }
    return 0;
}

Client* ClientList::getClient(int index)
{
   Client* client = _firstClient;
   int p = 0;
   while ( client != nullptr )
   {
       if ( p == index )
       {
           return client;
       }
       else
       {
           client = client->_nextClient;
           p++;
       }
   }
   return nullptr;
}


Client* ClientList::getClient(MQTTSNString* clientId)
{
    _mutex.lock(18);
    Client* client = _firstClient;
    const char* clID =clientId->cstring;

    if (clID == nullptr )
    {
        clID = clientId->lenstring.data;
    }

    while (client != nullptr)
    {
        if (strncmp((const char*)client->getClientId(), clID, MQTTSNstrlen(*clientId)) == 0 )
        {
            _mutex.unlock(18);
            return client;
        }
        client = client->_nextClient;
    }
    _mutex.unlock(18);
    return 0;
}

Client* ClientList::createClient(SensorNetAddress* addr, MQTTSNString* clientId, int type)
{
	return createClient(addr, clientId, false, false, type);
}

Client* ClientList::createClient(SensorNetAddress* addr, MQTTSNString* clientId, bool unstableLine, bool secure, int type)
{
    Client* client = nullptr;

    /*  anonimous clients */
    if ( _clientCnt > MAX_CLIENTS )
    {
        return 0;  // full of clients
    }

    client = getClient(addr);
    if ( client )
    {
        return client;
    }

    /* creat a new client */
    client = new Client(secure);
    if ( addr )
    {
        client->setClientAddress(addr);
    }
    client->setSensorNetType(unstableLine);
    if ( MQTTSNstrlen(*clientId) )
    {
        client->setClientId(*clientId);
    }
    else
    {
        MQTTSNString  dummyId MQTTSNString_initializer;
        dummyId.cstring = strdup("");
        client->setClientId(dummyId);
         free(dummyId.cstring);
    }

    if ( type == AGGREGATER_TYPE )
    {
    	client->setAggregated();
    }
    else if ( type == QOSM1PROXY_TYPE )
    {
        client->setQoSm1();
    }

    _mutex.lock(19);

    /* add the list */
    if ( _firstClient == nullptr )
    {
        _firstClient = client;
        _endClient = client;
    }
    else
    {
        _endClient->_nextClient = client;
        client->_prevClient = _endClient;
        _endClient = client;
    }
    _clientCnt++;
    _mutex.unlock(19);
    return client;
}

Client* ClientList::createPredefinedTopic( MQTTSNString* clientId, string topicName, uint16_t topicId, bool aggregate)
{
	if ( topicId == 0 )
	{
		WRITELOG("Invalid TopicId. Predefined Topic %s,  TopicId is 0. \n", topicName.c_str());
		return nullptr;
	}

	if ( strcmp(clientId->cstring, common_topic) == 0 )
	{
		theGateway->getTopics()->add((const char*)topicName.c_str(), topicId);
		return nullptr;
	}
	else
	{
		Client* client = getClient(clientId);

		if ( _authorize && client == nullptr )
		{
			return nullptr;
		}

		/*  anonimous clients */
		if ( _clientCnt > MAX_CLIENTS )
		{
			return nullptr;  // full of clients
		}

		if ( client == nullptr )
		{
			/* creat a new client */
			client = new Client();
			client->setClientId(*clientId);
			if ( aggregate )
			{
				client->setAggregated();
			}
			_mutex.lock(20);

			/* add the list */
			if ( _firstClient == nullptr )
			{
				_firstClient = client;
				_endClient = client;
			}
			else
			{
				_endClient->_nextClient = client;
				client->_prevClient = _endClient;
				_endClient = client;
			}
			_clientCnt++;
			_mutex.unlock(20);
		}

		// create Topic & Add it
		client->getTopics()->add((const char*)topicName.c_str(), topicId);
		client->_hasPredefTopic = true;
		return client;
	}
}

uint16_t ClientList::getClientCount()
{
    return _clientCnt;
}

bool ClientList::isAuthorized()
{
    return _authorize;
}


