#include<stdint.h>
#include<sys/eventfd.h>
#include<stdio.h>
#include<unistd.h>
#include<fcntl.h>
#include<errno.h>

int main()
{
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(efd == -1)
    {
        perror("eventfd failed");
        return -1;
    }
    uint64_t value = 1;
    write(efd, &value, sizeof(value));
    write(efd, &value, sizeof(value));
    write(efd, &value, sizeof(value));
    uint64_t res = 0;
    read(efd, &res, sizeof(res));
    printf("res: %lu\n", res);
    read(efd, &res, sizeof(res));
    printf("res: %lu\n", res);
    read(efd, &res, sizeof(res));
    printf("res: %lu\n", res);
    close(efd);
    return 0;
}