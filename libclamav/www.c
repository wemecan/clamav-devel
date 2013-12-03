#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <ctype.h>

#include <sys/types.h>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

#include "libclamav/others.h"
#include "libclamav/clamav.h"
#include "libclamav/www.h"

int connect_host(const char *host, const char *port)
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;

    memset(&hints, 0x00, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &servinfo))
        return -1;

    for (p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0)
            continue;

        if (connect(sockfd, p->ai_addr, p->ai_addrlen)) {
            close(sockfd);
            continue;
        }

        /* Connected to host */
        break;
    }

    if (!(p)) {
        freeaddrinfo(servinfo);
        close(sockfd);
        return -1;
    }

    freeaddrinfo(servinfo);

    return sockfd;
}

size_t encoded_size(const char *postdata)
{
    const char *p;
    size_t len=0;

    for (p = postdata; *p != '\0'; p++)
        len += isalnum(*p) ? 1 : 3;

    return len;
}

char *encode_data(const char *postdata)
{
    char *buf;
    size_t bufsz, i, j;

    bufsz = encoded_size(postdata);
    if (bufsz == 0)
        return NULL;

    buf = cli_calloc(1, bufsz+1);
    if (!(buf))
        return NULL;

    for (i=0, j=0; postdata[i] != '\0'; i++) {
        if (isalnum(postdata[i])) {
            buf[j++] = postdata[i];
        } else {
            sprintf(buf+j, "%%%02x", postdata[i]);
            j += 3;
        }
    }

    return buf;
}

void submit_post(const char *host, const char *port, const char *url, const char *postdata)
{
    int sockfd;
    unsigned int i;
    char *buf;
    size_t bufsz;
    char chunkedlen[21];

    snprintf(chunkedlen, sizeof(chunkedlen), "%X\r\n", strlen(postdata));

    bufsz = sizeof("POST   HTTP/1.1") + 1; /* Yes. Three blank spaces. +1 for the \n */
    bufsz += strlen(url);
    bufsz += sizeof("Host: \n");
    bufsz += strlen(host);
    bufsz += sizeof("Connection: Close\n");
    bufsz += sizeof("Transfer-Encoding: Chunked\n");
    bufsz += 2; /* +2 for \n\n */
    bufsz += strlen(chunkedlen) + 9; /* 9 for "\r\n(data)0\r\n\r\n\0" */
    bufsz += strlen(postdata) + 1;
    bufsz += 2; /* Terminating \n\n */

    buf = cli_calloc(1, bufsz);
    if (!(buf)) {
        return;
    }

    snprintf(buf, bufsz, "POST %s HTTP/1.1\n", url);
    snprintf(buf+strlen(buf), bufsz-strlen(buf), "Host: %s\n", host);
    snprintf(buf+strlen(buf), bufsz-strlen(buf), "Connection: Close\n");
    snprintf(buf+strlen(buf), bufsz-strlen(buf), "Transfer-encoding: Chunked\n\n");
    snprintf(buf+strlen(buf), bufsz-strlen(buf), "%s%s\r\n0\r\n\r\n", chunkedlen, postdata);

    sockfd = connect_host(host, port);
    if (sockfd < 0) {
        free(buf);
        return;
    }

    send(sockfd, buf, strlen(buf), 0);

    while (1) {
        /*
         * Check to make sure the stats submitted okay (so that we don't kill the HTTP request
         * while it's being processed).
         *
         * TODO: Add a time limit based on a call to select() to prevent lock-ups or major
         * slow downs.
         */
        memset(buf, 0x00, bufsz);
        if (recv(sockfd, buf, bufsz, 0) <= 0)
            break;

        if (strstr(buf, "STATOK"))
            break;
    }

    close(sockfd);
    free(buf);
}
