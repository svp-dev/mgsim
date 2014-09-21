#include "demo/prodcons.h"
#include "sim/delegate.h"

ExampleConsumer::ExampleConsumer(const std::string& name, Simulator::Object& parent, Simulator::Clock& clock, size_t sz)
    : Simulator::Object(name, parent),
      p_Consume(*this, "consume", Simulator::delegate::create<ExampleConsumer, &ExampleConsumer::DoConsume>(*this)),
      m_fifo ("b_fifo", *this, clock, sz)
{
    m_fifo.Sensitive(p_Consume);
}

Simulator::Result
ExampleConsumer::DoConsume()
{
    assert(!m_fifo.Empty());
    const int &x = m_fifo.Front();

    COMMIT {
	std::cout << "cycle " << GetKernel()->GetCycleNo() << ": consumer takes " << x << std::endl;
    }
    m_fifo.Pop();
    return Simulator::SUCCESS;
}


ExampleProducer::ExampleProducer(const std::string& name, Simulator::Object& parent, Simulator::Clock& clock, ExampleConsumer& cons)
    : Simulator::Object(name, parent),
      m_counter(0),
      m_fifo(cons.m_fifo),
      p_Produce(*this, "produce", Simulator::delegate::create<ExampleProducer, &ExampleProducer::DoProduce>(*this)),
      m_enabled("f_enabled", *this, clock, true)
{
    m_enabled.Sensitive(p_Produce);
}

Simulator::Result
ExampleProducer::DoProduce()
{
    int x = m_counter;

    if (!m_fifo.Push(x))
    {
	std::cout << "cycle " << GetKernel()->GetCycleNo() << ": producer stalls" << std::endl;
	return Simulator::FAILED;
    }
    COMMIT {
	std::cout << "cycle " << GetKernel()->GetCycleNo() << ": producer produces " << x << std::endl;
	++m_counter;
    }
    return Simulator::SUCCESS;
}
