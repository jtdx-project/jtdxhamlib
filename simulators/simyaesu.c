// can run this using rigctl/rigctld and socat pty devices
// gcc -o simyaesu simyaesu.c
#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE 256

float freqA = 14074000;
float freqB = 14074500;

// ID 0310 == 310, Must drop leading zero
typedef enum nc_rigid_e
{
    NC_RIGID_NONE            = 0,
    NC_RIGID_FT450           = 241,
    NC_RIGID_FT450D          = 244,
    NC_RIGID_FT950           = 310,
    NC_RIGID_FT891           = 135,
    NC_RIGID_FT991           = 135,
    NC_RIGID_FT2000          = 251,
    NC_RIGID_FT2000D         = 252,
    NC_RIGID_FTDX1200        = 583,
    NC_RIGID_FTDX9000D       = 101,
    NC_RIGID_FTDX9000Contest = 102,
    NC_RIGID_FTDX9000MP      = 103,
    NC_RIGID_FTDX5000        = 362,
    NC_RIGID_FTDX3000        = 460,
    NC_RIGID_FTDX101D        = 681,
    NC_RIGID_FTDX101MP       = 682
} nc_rigid_t;

int
getmyline(int fd, char *buf)
{
    char c;
    int i = 0;
    memset(buf, 0, BUFSIZE);

    while (read(fd, &c, 1) > 0)
    {
        buf[i++] = c;

        if (c == ';') { return strlen(buf); }
    }

    return strlen(buf);
}

#if defined(WIN32) || defined(_WIN32)
int openPort(char *comport) // doesn't matter for using pts devices
{
    int fd;
    fd = open(comport, O_RDWR);

    if (fd < 0)
    {
        perror(comport);
    }

    return fd;
}

#else
int openPort(char *comport) // doesn't matter for using pts devices
{
    int fd = posix_openpt(O_RDWR);
    char *name = ptsname(fd);

    if (name == NULL)
    {
        perror("pstname");
        return -1;
    }

    printf("name=%s\n", name);

    if (fd == -1 || grantpt(fd) == -1 || unlockpt(fd) == -1)
    {
        perror("posix_openpt");
        return -1;
    }

    return fd;
}
#endif



int main(int argc, char *argv[])
{
    char buf[256];
    char *pbuf;
    int n;
    int fd = openPort(argv[1]);

    while (1)
    {
        if (getmyline(fd, buf))
        {
            printf("Cmd:%s\n", buf);
        }
        else { return 0; }

        if (strcmp(buf, "RM5;") == 0)
        {
            printf("%s\n", buf);
            usleep(50 * 1000);
            pbuf = "RM5100000;";
            n = write(fd, pbuf, strlen(pbuf));
            printf("n=%d\n", n);

            if (n <= 0) { perror("RM5"); }
        }

        if (strcmp(buf, "AN0;") == 0)
        {
            printf("%s\n", buf);
            usleep(50 * 1000);
            pbuf = "AN030;";
            n = write(fd, pbuf, strlen(pbuf));
            printf("n=%d\n", n);

            if (n <= 0) { perror("AN"); }
        }
        else if (strcmp(buf, "IF;") == 0)
        {
            printf("%s\n", buf);
            usleep(50 * 1000);
            pbuf = "IF059014200000+000000700000;";
            n = write(fd, pbuf, strlen(pbuf));
            printf("n=%d\n", n);

            if (n <= 0) { perror("IF"); }
        }
        else if (strcmp(buf, "ID;") == 0)
        {
            printf("%s\n", buf);
            usleep(50 * 1000);
            int id = NC_RIGID_FTDX3000;
            snprintf(buf, sizeof(buf), "ID%03d;", id);
            n = write(fd, buf, strlen(buf));
            printf("n=%d\n", n);

            if (n <= 0) { perror("ID"); }
        }
        else if (strcmp(buf, "AI;") == 0)
        {
            printf("%s\n", buf);
            usleep(50 * 1000);
            snprintf(buf, sizeof(buf), "AI0;");
            n = write(fd, buf, strlen(buf));
            printf("n=%d\n", n);

            if (n <= 0) { perror("ID"); }
        }

#if 0
        else if (strncmp(buf, "AI", 2) == 0)
        {
            if (strcmp(buf, "AI;"))
            {
                printf("%s\n", buf);
                usleep(50 * 1000);
                n = fprintf(fp, "%s", "AI0;");
                printf("n=%d\n", n);

                if (n <= 0) { perror("AI"); }
            }
        }

#endif
        else if (strcmp(buf, "VS;") == 0)
        {
            printf("%s\n", buf);
            usleep(50 * 1000);
            pbuf = "VS0;";
            n = write(fd, pbuf, strlen(pbuf));
            printf("n=%d\n", n);

            if (n < 0) { perror("VS"); }
        }
        else if (strcmp(buf, "EX032;") == 0)
        {
            static int ant = 0;
            ant = (ant + 1) % 3;
            printf("%s\n", buf);
            usleep(50 * 1000);
            snprintf(buf, sizeof(buf), "EX032%1d;", ant);
            n = write(fd, buf, strlen(buf));
            printf("n=%d\n", n);

            if (n < 0) { perror("EX032"); }
        }

        else if (strlen(buf) > 0)
        {
            fprintf(stderr, "Unknown command: %s\n", buf);
        }

    }

    return 0;
}
