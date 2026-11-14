#pragma once

#include <memory>
#include <string>
#include <vector>

namespace core {

class Document;

class Command {
public:
    virtual ~Command() = default;
    virtual void undo(Document& doc) = 0;
    virtual void redo(Document& doc) = 0;
    virtual std::string label() const = 0;
    // Absorbs `next` (already applied, same kind, same target) so both undo
    // as one step; false if it can't. Used for continuous edits (scrubbing).
    virtual bool mergeWith(const Command& next) { (void)next; return false; }
    // False for commands that only change state kept with the document but
    // not saved (the selection): they don't make it differ from its file.
    virtual bool changesContent() const { return true; }
    // About how much memory the command keeps (pixel data above all), for the history's memory limit.
    // Images shared with the document or other commands count each time, so it errs on the high side.
    virtual size_t bytes() const { return 256; }
};

class UndoStack {
public:
    // Applies the command, then records it. With `merge`, it folds into the
    // previous command when that one accepts it (never into the saved state).
    void execute(Document& doc, std::unique_ptr<Command> command, bool merge = false);
    // Records a command whose effect has already been applied.
    void push(std::unique_ptr<Command> command);

    // How many commands are applied (the same number again means no edit in between).
    size_t position() const { return m_index; }
    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index < m_commands.size(); }
    void undo(Document& doc);
    void redo(Document& doc);
    void clear();

    // The clean state is the point in history that matches the saved file;
    // commands that don't change content (see Command) don't move away from it.
    void setClean();
    // No point in the history matches the file any more (e.g. a document
    // restored after a crash).
    void setUnsaved() { m_cleanValid = false; }
    bool isClean() const;

    std::string undoLabel() const;
    std::string redoLabel() const;

    // Maximum number of commands kept; 0 means unlimited.
    void setLimit(size_t limit);
    // Memory the history may keep, in bytes (0 = unlimited): the oldest commands go first, but the
    // newest one always stays.
    void setByteLimit(size_t bytes);
    size_t bytes() const;

private:
    std::vector<std::unique_ptr<Command>> m_commands;
    void dropOldest(size_t count);
    // True if no command in [from, to) changes content.
    bool contentSame(size_t from, size_t to) const;

    size_t m_index = 0; // commands before m_index are applied
    size_t m_limit = 0;
    size_t m_byteLimit = 0;
    void trim();
    size_t m_clean = 0;
    bool m_cleanValid = true; // false once the clean state can't be reached again
};

} // namespace core
