#include "DRISC.h"

namespace Simulator
{

DRISC::Pipeline::PipeAction DRISC::Pipeline::DummyStage::OnCycle()
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

DRISC::Pipeline::DummyStage::DummyStage(const std::string& name, Pipeline& parent, Clock& clock, const MemoryWritebackLatch& input, MemoryWritebackLatch& output, Config& /*config*/)
  : Stage(name, parent, clock),
    m_input(input),
    m_output(output)
{
}

}
