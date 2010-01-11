#include "Pipeline.h"
#include "Processor.h"
#include <cassert>
using namespace std;

namespace Simulator
{

Pipeline::PipeAction Pipeline::DummyStage::OnCycle()
{
    COMMIT
    {
        (CommonData&)m_output = m_input;
        m_output.suspend = m_input.suspend;
        m_output.Rrc     = m_input.Rrc;
        m_output.Rc      = m_input.Rc;
        m_output.Rcv     = m_input.Rcv;
    }
    return PIPE_CONTINUE;
}

Pipeline::DummyStage::DummyStage(const std::string& name, Pipeline& parent, const MemoryWritebackLatch& input, MemoryWritebackLatch& output, const Config& /*config*/)
  : Stage(name, parent),
    m_input(input),
    m_output(output)
{
}

}
