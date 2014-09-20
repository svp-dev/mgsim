#ifndef PRODCONS2_H
#define PRODCONS2_H

#include "sim/kernel.h"
#include "sim/storage.h"
#include <vector>

namespace Simulator
{


    struct ConnectableReceiver
    {
	virtual ~ConnectableReceiver() {};
    };

    template<typename T>
    struct Receiver : ConnectableReceiver
    {
	virtual Result Deliver(const T& arg) = 0;
    };

    template<typename T, unsigned s = 0>
    struct ReceiverSlot : Receiver<T>
    { };

    template<typename T,
	     typename U, Result (U::*Method)(const T& arg),
	     unsigned s = 0>
    struct MethodReceiver : ReceiverSlot<T, s>
    {
	Result Deliver(const T& arg) override
	{
	    U& p = static_cast<U&>(*this);
	    return (p->*Method)(arg);
	}
    };

    struct ConnectableSender
    {
	virtual void Connect(ConnectableReceiver&) = 0;
	virtual ~ConnectableSender() {};
    };

    template<typename T>
    struct Sender : ConnectableSender
    {
	Receiver<T>* m_peer;
	Sender() : m_peer(0) {}
	Sender(const Sender&) = delete;
	Sender& operator=(const Sender&) = delete;

	Result Post(const T& arg) { return m_peer->Deliver(arg); }
	void Connect(ConnectableReceiver& peer) override
	{
	    auto& p = dynamic_cast<Receiver<T>& >(peer);
	    m_peer = &p;
	}
    };

    template<unsigned s, typename T>
    struct SenderSlot : Sender<T>
    { };


    struct Connector
    {
	virtual void Connect(ConnectableReceiver& r) = 0;
	virtual void Connect(ConnectableSender& s) = 0;
	virtual void Initialize() = 0;
	virtual ~Connector() {};
    };

    struct DirectConnector : Connector
    {
	ConnectableReceiver *m_r;
	ConnectableSender *m_s;
	DirectConnector() : m_r(0), m_s(0) {}
	DirectConnector(const DirectConnector&) = delete;
	DirectConnector& operator=(const DirectConnector&) = delete;

	void Connect(ConnectableReceiver& r) override { m_r = &r; }
	void Connect(ConnectableSender& s) override { m_s = &s; }
	void Initialize() override {
	    assert(m_r != 0);
	    assert(m_s != 0);
	    m_s->Connect(*m_r);
	}
    };

    template<typename T>
    struct BufferedConnector : Object, Receiver<T>, Connector
    {
	Receiver<T> *m_down_peer;
	ConnectableSender* m_up_peer;
	Buffer<T> m_fifo;


	BufferedConnector(const std::string &name,
			  Object& parent, Clock& clock, size_t sz)
	    : Object(name, parent, clock),
	      m_down_peer(0), m_up_peer(0),
	      m_fifo("b_fifo", *this, clock, sz),
	      p(*this, "p_proc", delegate::create<BufferedConnector<T>, &BufferedConnector<T>::OnCycle>(*this))
	{ 
	    m_fifo.Sensitive(p);
	}
	BufferedConnector(const BufferedConnector&) = delete;
	BufferedConnector& operator=(const BufferedConnector&) = delete;

	void Connect(ConnectableReceiver& r) override
	{
	    auto& rr = dynamic_cast<Receiver<T>& >(r);
	    m_down_peer = &rr;
	}

	void Connect(ConnectableSender& s) override
	{
	    m_up_peer = &s;
	}
	void Initialize() override
	{
	    assert(m_down_peer != NULL);
	    assert(m_up_peer != NULL);
	    m_up_peer->Connect(*this);
	}

	virtual Result Deliver(const T& arg) override
	{
	    if (!m_fifo.Push(arg))
		return FAILED;
	    return SUCCESS;
	}

	Process p;
	Result OnCycle()
	{
	    assert(!m_fifo.Empty());
	    const T& arg = m_fifo.Front();
	    Result r = m_down_peer->Deliver(arg);
	    if (r == SUCCESS)
		m_fifo.Pop();
	    return r;
	}



    };
}


class ExampleConsumer2 : public Simulator::Object, public Simulator::Receiver<int>
{
public:
    ExampleConsumer2(const std::string& name, Simulator::Object& parent, Simulator::Clock& clock);
    
    Simulator::Result   Deliver(const int&) override;
};

class ExampleProducer2 : public Simulator::Object, public Simulator::Sender<int>
{
public:
    ExampleProducer2(const std::string& name, Simulator::Object& parent, Simulator::Clock& clock);
    
private:
    Simulator::Result DoProduce();
    
    int                     m_counter;
    
    Simulator::Process      p_Produce;
    Simulator::SingleFlag   m_enabled;
};

#endif
