#include "storage.h"

namespace Simulator
{

void SensitiveStorage::Notify()
{
    m_kernel.ActivateProcess(m_handle);
}

void SensitiveStorage::Unnotify()
{
    if (--m_handle->activations == 0)
    {
        // Remove the handle node from the list
        *m_handle->pPrev = m_handle->next;
        if (m_handle->next != NULL) {
            m_handle->next->pPrev = m_handle->pPrev;
        }
        
        m_handle->state = STATE_IDLE;
    }
}

SensitiveStorage::SensitiveStorage(Kernel& kernel, IComponent& component, int state)
    : Storage(kernel)
{
    m_component = &component;
    m_state     = state;
}

}
