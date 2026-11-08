module aegis.editor.core;
import std;

namespace aegis::editor
{
void FullViewport::toggle()
{
    m_active = !m_active;
    m_enteredByPlay = false;
}

void FullViewport::leave()
{
    m_active = false;
    m_enteredByPlay = false;
}

void FullViewport::play(bool onPlay)
{
    if (!onPlay || m_active)
        return;
    m_active = true;
    m_enteredByPlay = true;
}

void FullViewport::stop()
{
    if (m_enteredByPlay)
        m_active = false;
    m_enteredByPlay = false;
}
}
