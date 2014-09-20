#include "demo/prodcons2.h"
#include "sim/delegate.h"

ExampleConsumer2::ExampleConsumer2(const std::string& name, Simulator::Object& parent, Simulator::Clock& clock)
    : Simulator::Object(name, parent, clock)
{

}

Simulator::Result
ExampleConsumer2::Deliver(const int& x)
{
    COMMIT {
	std::cout << "cycle " << GetKernel()->GetCycleNo() << ": consumer takes " << x << std::endl;
    }

    return Simulator::SUCCESS;
}


ExampleProducer2::ExampleProducer2(const std::string& name, Simulator::Object& parent, Simulator::Clock& clock)
    : Simulator::Object(name, parent, clock),
      m_counter(0),
      p_Produce(*this, "produce", Simulator::delegate::create<ExampleProducer2, &ExampleProducer2::DoProduce>(*this)),
      m_enabled("f_enabled", *this, clock, true)
{
    m_enabled.Sensitive(p_Produce);
}

Simulator::Result
ExampleProducer2::DoProduce()
{
    int x = m_counter;

    if (this->Post(x) != Simulator::SUCCESS)
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
