#include "kernel/types.h"
#include "user/user.h"

int main (int argc, char *argv[]) {
    int p2c[2], c2p[2];
    pipe(p2c);
    pipe(c2p);
    if (fork() == 0) {
        close(c2p[0]);
        close(p2c[1]);

        int ppid;
        int pid = getpid();

        if (read(p2c[0], &ppid, sizeof(ppid)) < 0) {
            fprintf(2, "read error\n");
            exit(1);
        }
        close(p2c[0]);

        printf("%d: received ping from pid %d\n", pid, ppid);

        if (write(c2p[1], &pid, sizeof(pid)) != sizeof(pid)) {
            fprintf(2, "write error\n");
            exit(1);
        }
        close(c2p[1]);
    } else {
        close(p2c[0]);
        close(c2p[1]);
        
        int cpid;
        int pid = getpid();

        if (write(p2c[1], &pid, sizeof(pid)) != sizeof(pid)) {
            fprintf(2, "write error\n");
            exit(1);
        }
        close(p2c[1]);

        if (read(c2p[0], &cpid, sizeof(cpid)) < 0) {
            fprintf(2, "read error\n");
            exit(1);
        }
        close(c2p[0]);

        printf("%d: received pong from pid %d\n", pid, cpid);
    }
    exit(0);
}