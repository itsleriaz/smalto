#include "core/Undo.h"

#include <algorithm>

namespace core {

void UndoStack::execute(Document& doc, std::unique_ptr<Command> command, bool merge)
{
    command->redo(doc);
    // Never into the saved state: the top command must come after it.
    const bool topIsSaved = m_cleanValid && m_clean == m_index;
    if (merge && canUndo() && m_index == m_commands.size() && !topIsSaved
        && m_commands[m_index - 1]->mergeWith(*command)) {
        trim(); // a merged patch may have grown
        return;
    }
    push(std::move(command));
}

bool UndoStack::contentSame(size_t from, size_t to) const
{
    for (size_t i = from; i < to && i < m_commands.size(); ++i)
        if (m_commands[i]->changesContent())
            return false;
    return true;
}

bool UndoStack::isClean() const
{
    return m_cleanValid && contentSame(std::min(m_clean, m_index), std::max(m_clean, m_index));
}

void UndoStack::push(std::unique_ptr<Command> command)
{
    // The clean state is in the redo branch about to go: still reachable in
    // content if nothing on the way there changed any.
    if (m_clean > m_index) {
        if (m_cleanValid && contentSame(m_index, m_clean))
            m_clean = m_index;
        else
            m_cleanValid = false;
    }
    m_commands.resize(m_index); // drop the redo branch
    m_commands.push_back(std::move(command));
    m_index = m_commands.size();
    trim();
}

size_t UndoStack::bytes() const
{
    size_t total = 0;
    for (const auto& c : m_commands)
        total += c->bytes();
    return total;
}

void UndoStack::trim()
{
    if (m_limit > 0 && m_commands.size() > m_limit)
        dropOldest(m_commands.size() - m_limit);
    if (m_byteLimit == 0 || m_commands.size() < 2)
        return;
    size_t total = bytes();
    size_t drop = 0;
    while (total > m_byteLimit && drop + 1 < m_commands.size())
        total -= m_commands[drop++]->bytes();
    if (drop > 0)
        dropOldest(drop);
}

void UndoStack::setByteLimit(size_t bytes)
{
    m_byteLimit = bytes;
    trim();
}

void UndoStack::dropOldest(size_t count)
{
    m_commands.erase(m_commands.begin(), m_commands.begin() + ptrdiff_t(count));
    m_index = m_index > count ? m_index - count : 0;
    if (m_clean >= count)
        m_clean -= count;
    else if (m_cleanValid && contentSame(m_clean, count))
        m_clean = 0; // the oldest kept state has the saved content
    else
        m_cleanValid = false;
}

void UndoStack::undo(Document& doc)
{
    if (canUndo())
        m_commands[--m_index]->undo(doc);
}

void UndoStack::redo(Document& doc)
{
    if (canRedo())
        m_commands[m_index++]->redo(doc);
}

void UndoStack::clear()
{
    m_commands.clear();
    m_index = 0;
    m_clean = 0;
    m_cleanValid = true;
}

void UndoStack::setClean()
{
    m_clean = m_index;
    m_cleanValid = true;
}

std::string UndoStack::undoLabel() const
{
    return canUndo() ? m_commands[m_index - 1]->label() : std::string();
}

std::string UndoStack::redoLabel() const
{
    return canRedo() ? m_commands[m_index]->label() : std::string();
}

void UndoStack::setLimit(size_t limit)
{
    m_limit = limit;
    if (m_limit > 0 && m_commands.size() > m_limit)
        dropOldest(m_commands.size() - m_limit);
}

} // namespace core
