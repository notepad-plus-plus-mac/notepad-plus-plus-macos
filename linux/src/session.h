#ifndef SESSION_H
#define SESSION_H

/* Serialise all open (saved) tabs to ~/.config/notetux/session.xml.
 * Call before closing tabs so positions are still readable. */
void session_save(void);

/* Reopen tabs from ~/.config/notetux/session.xml and restore scroll/caret.
 * Silently skips files that no longer exist on disk. */
void session_restore(void);

#endif /* SESSION_H */
