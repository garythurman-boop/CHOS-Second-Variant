/* Native descriptors are process-owned snapshots/working copies. Spawn always
 * creates an empty child table; there is no exec/fork or descriptor inheritance.
 * Do not emulate Unix fcntl/dup/sockets with host flags or success stubs. */
#include "config.h"
#include <wtf/UniStdExtras.h>
#include <chos/user.h>
#include <cerrno>
namespace WTF {
static bool ownedFile(int fd)
{
    chos_file_metadata_t info;
    auto result = chos_file_metadata(fd, &info);
    if (result < 0) { errno = -result; return false; }
    return true;
}
bool setCloseOnExec(int fd) { return ownedFile(fd); }
bool unsetCloseOnExec(int fd) { if (ownedFile(fd)) errno = ENOTSUP; return false; }
int dupCloseOnExec(int fd) { if (ownedFile(fd)) errno = ENOTSUP; return -1; }
bool setNonBlock(int fd)
{
    // Native regular-file reads/writes operate on already-owned memory. Explicit
    // open/flush can perform storage I/O; there is no file readiness queue to set.
    return ownedFile(fd);
}
}
