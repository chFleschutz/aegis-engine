module aegis.editor.core;
import std;

namespace aegis::editor
{
auto SettledExtent::update(PixelSize requested) -> PixelSize
{
    requested = { std::max(requested.width, 1u), std::max(requested.height, 1u) };
    if (!m_current || requested == *m_current)
    {
        m_current = requested;
        m_stableFrames = 0;
        return *m_current;
    }

    if (requested == m_pending)
        ++m_stableFrames;
    else
    {
        m_pending = requested;
        m_stableFrames = 1;
    }
    if (m_stableFrames >= settleFrames)
    {
        m_current = requested;
        m_stableFrames = 0;
    }
    return *m_current;
}

void SettledExtent::reset()
{
    m_current.reset();
    m_stableFrames = 0;
}
}
