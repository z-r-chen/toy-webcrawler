#include <arpa/inet.h>  //inet_ntoa(),ntohs()
#include <assert.h>
#include <limits.h>
#include <netdb.h>  //getaddrinfo()
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>  //bzero()
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>  //close()

#include "hash_table.h"
#include "queue.h"

#define PORT "80"
#define HEADLEN 64
#define BUFLEN 256
#define MSGLEN 16384
#define LINKLEN 64
#define LINK_COUNT 512
#define PROG "crawler"

#define MIN(a, b) (a) <= (b) ? (a) : (b)

void resourceError(int status, char *caller) {
    printf("%s: resource error status=%d\n", PROG, status);
    perror(caller);
    exit(2);
}

void requestGET(char *src, char *link) {
    char r[HEADLEN];
    strcpy(r, "GET ");
    strcat(r, link);
    strcat(r, " HTTP/1.0\r\n\r\n");
    strcpy(src, r);
}

void requestHEAD(char *src, char *link) {
    char r[HEADLEN];
    strcpy(r, "HEAD ");
    strcat(r, link);
    strcat(r, " HTTP/1.0\r\n\r\n");
    strcpy(src, r);
}

int main(int argc, char *argv[]) {
    int err, nbytes_total, nbytes_sent, nbytes_received, nbytes;
    int sockfd;                      // client socket's file desrciptor
    struct addrinfo hints, *server;  // server address info and hints
    struct sockaddr_in serverAddr;   // info of sock (of server)
    socklen_t addrlen;

    // expected command line input:
    // ./crawler domain_name port
    // argc = 3

    // nothing has been specified
    if (argc < 3 || argc >= 4) {
        printf("Usage: ./crawler <domain_name> <port>\n");
        exit(1);
    }

    char *host_name = argv[1];
    char *port = argv[2];

    bzero(&hints, sizeof(hints));     // use defaults unless overridden
    hints.ai_family = AF_INET;        // IPv4; for IPv6, use AF_INET6
    hints.ai_socktype = SOCK_STREAM;  // for TCP
    hints.ai_protocol = 0;            // any protocol

    /* ----- data structures for collecting information ----- */
    Pseudo_HashTable *page_table = init_table(LINK_COUNT);
    Pseudo_HashTable *img_table = init_table(LINK_COUNT);
    Pseudo_HashTable *not_found_table = init_table(LINK_COUNT);
    Pseudo_HashTable *redirect_table = init_table(LINK_COUNT);
    Pseudo_HashTable *redirect_dest = init_table(LINK_COUNT);
    Pseudo_HashTable *offsite_host_table = init_table(LINK_COUNT);
    Pseudo_HashTable *offsite_dest_table = init_table(LINK_COUNT);
    Pseudo_HashTable *offsite_offer_table = init_table(LINK_COUNT);
    int offsite_ports[LINK_COUNT];
    Queue *queue = init_queue(LINK_COUNT);  // for BFS

    char buf[BUFLEN];
    char response[MSGLEN];

    char link[LINKLEN];
    strcpy(link, "/");
    char request[HEADLEN];
    enqueue(queue, link);
    insert(page_table, link);

    int trigger = 500;  // 2020ms to keep the politeness
    bool is_initial_request = true;

    int min_size = INT_MAX;
    char min_size_page[LINKLEN];
    int max_size = 0;
    char max_size_page[LINKLEN];

    time_t oldest_t;
    time_t recent_t;
    char oldest_page[LINKLEN];
    char most_recent_modified_page[LINKLEN];

    /* ----- apply BFS to recursively crawl the website ----- */
    while (!isEmpty(queue)) {
        // delay if it's not the initial request
        if (!is_initial_request) {
            int msec = 0;
            clock_t before = clock();
            do {
                clock_t diff = clock() - before;
                msec = diff * 1000 / CLOCKS_PER_SEC;
            } while (msec < trigger);
        }
        // get the address information of the server
        if ((err = getaddrinfo(host_name, port, &hints, &server))) {
            printf("Fail to get the address info.\n");
            exit(1);
        }
        // create client's socket
        sockfd =
            socket(server->ai_family, server->ai_socktype, server->ai_protocol);
        if (sockfd < 0) {
            resourceError(sockfd, "socket");
        }
        // attempt to connect to the server
        if ((err = connect(sockfd, server->ai_addr, server->ai_addrlen)) < 0) {
            resourceError(err, "connect");
        }
        addrlen = sizeof(serverAddr);
        getsockname(sockfd, (struct sockaddr *)&serverAddr, &addrlen);
        // send the request to the server
        strcpy(link, dequeue(queue));
        requestGET(request, link);
        nbytes_total = strlen(request);
        nbytes_sent = 0;
        do {
            nbytes = write(sockfd, request + nbytes_sent,
                           nbytes_total - nbytes_sent);
            if (nbytes < 0) {
                printf("ERROR writing message to socket\n");
                exit(1);
            }
            if (nbytes == 0) {
                break;
            }
            nbytes_sent += nbytes;
        } while (nbytes_sent < nbytes_total);
        if (nbytes_total < 0) {
            resourceError(nbytes_total, "send");
        }
        printf("%s: sent message (%d bytes): %s", PROG, nbytes_total, request);
        // receive reply from server
        bzero(buf, sizeof(buf));
        bzero(response, sizeof(response));
        nbytes_received = 0;
        do {
            nbytes = recv(sockfd, buf, sizeof(buf), 0);
            if (nbytes < 0) {
                printf("ERROR receiving message to socket\n");
                exit(1);
            }
            if (nbytes == 0) {
                break;
            }
            buf[MIN(nbytes, (sizeof(buf)))] = 0;
            nbytes_received += nbytes;
            strcat(response, buf);
        } while (true);

        char *sp = response;
        char *lp, *rp;

        /* --- extract status --- */
        char *recog_http = "HTTP/1.1 ";
        int statusFlag = 2;  // 200 by default (hopefully)
        sp = strstr(sp, recog_http);
        lp = sp + strlen(recog_http);
        for (rp = lp; *rp != ' '; ++rp) {
        }
        char status[4];
        memcpy(status, lp, rp - lp);

        if (strcmp(status, "404") == 0) {
            // page not found
            statusFlag = 4;
            if (search(not_found_table, hash_djb2(link)) == NULL) {
                insert(not_found_table, link);
            }
        } else if (strcmp(status, "301") == 0 || strcmp(status, "302") == 0) {
            // redirects
            statusFlag = 3;
            if (search(redirect_table, hash_djb2(link)) == NULL) {
                insert(redirect_table, link);
            }
        }

        /* --- extract dates and last-modified --- */
        if (statusFlag == 2) {
            char *recog_modified = "Last-Modified: ";
            sp = strstr(sp, recog_modified);
            lp = sp + strlen(recog_modified) + 5;
            rp = strstr(sp, "GMT") - 1;
            char date[20];
            memcpy(date, lp, rp - lp);
            struct tm tm = {0};
            char *parsedDate = strptime(date, "%d %b %Y %H:%M:%S", &tm);
            if (parsedDate == NULL) {
                printf("cannot parse the date\n");
                exit(1);
            }

            // keep track of oldest and recent-modified
            time_t t = mktime(&tm);
            if (is_initial_request) {
                oldest_t = t;
                recent_t = t;
                strcpy(oldest_page, "/");
                strcpy(most_recent_modified_page, "/");
            } else {
                double secs = difftime(t, oldest_t);
                if (secs < 0) {
                    // t < oldest_t
                    oldest_t = t;
                    strcpy(oldest_page, link);
                }
                secs = difftime(t, recent_t);
                if (secs > 0) {
                    // t > recent_t
                    recent_t = t;
                    strcpy(most_recent_modified_page, link);
                }
            }
        }

        /* --- extract content-length --- */
        if (statusFlag == 2) {  // we don't count the length of 30x or 404 pages
                                // given that they are not real pages
            char *recog_length = "Content-Length: ";
            sp = strstr(sp, recog_length);
            lp = sp + strlen(recog_length);
            for (rp = lp; *rp != 'V'; ++rp) {
            }
            char contentLength[rp - lp + 2];
            memcpy(contentLength, lp, rp - lp);
            int local_len = atoi(contentLength);
            if (local_len < min_size) {
                min_size = local_len;
                strcpy(min_size_page, link);
            }
            if (local_len > max_size) {
                max_size = local_len;
                strcpy(max_size_page, link);
            }
        }

        /* --- extract links and images --- */
        char local_host[LINKLEN];
        char local_port[4];
        char local_link[LINKLEN];
        char image[LINKLEN];
        char *recog_link = "<a href=\"";
        char *recog_img = "<img src=\"";
        while (statusFlag != 4 && sp - response <= MSGLEN) {
            char *ti = strstr(sp, recog_img);   // temp pointer for img
            char *tl = strstr(sp, recog_link);  // temp pointer for link

            if (ti != NULL && tl != NULL) {
                sp = ti < tl ? ti : tl;
            } else if (ti == NULL && tl != NULL) {
                sp = tl;
            } else if (ti != NULL && tl != NULL) {
                sp = ti;
            } else {  // no more links or images on the page
                break;
            }

            if (sp == ti) {
                // analyse the path, locate the folder containing the image
                lp = link + 1;  // *link should be '/'
                char *last;
                for (rp = lp; *rp != 0; ++rp) {
                    if (*rp == '/') {  // it's under a folder
                        last = rp;
                    }
                }
                bzero(image, sizeof(image));
                memcpy(image, lp - 1, last - lp + 2);
                // extract the image path
                lp = sp + strlen(recog_img);
                for (rp = lp; *rp != '"'; ++rp) {
                }
                bzero(local_link, sizeof(local_link));
                memcpy(local_link, lp, rp - lp);
                strcat(image, local_link);
                if (search(img_table, hash_djb2(image)) == NULL) {
                    insert(img_table, image);
                }
                sp = rp;
            } else if (sp == tl) {
                // analyse and filter the link
                bzero(local_link, sizeof(local_link));
                bool includeProtocol = false;
                bool is_external_site = false;

                lp = sp + strlen(recog_link);
                for (rp = lp; *rp != '"'; ++rp) {
                }
                memcpy(local_link, lp, rp - lp);

                bzero(local_link, sizeof(local_link));
                bzero(local_host, sizeof(local_host));
                bzero(local_port, sizeof(local_port));

                for (lp = sp + strlen(recog_link); *lp != '"'; ++lp) {
                    if (*lp == '/' && *(lp + 1) == '/') {
                        // the link includes "http(s)://"
                        includeProtocol = true;
                        lp = lp + 2;
                        for (rp = lp; *rp != '"'; ++rp) {
                            if (*rp == ':' || *rp == '/') {
                                memcpy(local_host, lp, rp - lp);
                                break;
                            }
                        }
                        if (*rp == '"') {
                            memcpy(local_host, lp,
                                   rp - lp);  // local_link <- host_name
                        }
                        if (strcmp(local_host, host_name) !=
                            0) {  // offsite urls
                            is_external_site = true;
                        }
                        if (*(rp + 1) !=
                            '"') {  // has subdomain, continue to parse
                            if (*rp == ':') {  // port has been specified
                                lp = rp + 1;
                                for (; *rp != '/'; ++rp) {
                                }
                                memcpy(local_port, lp, rp - lp);
                            }
                            lp = rp;
                            for (; *rp != '"'; ++rp) {
                            }
                            memcpy(local_link, lp, rp - lp);
                        }

                        break;
                    }
                }

                if (!includeProtocol) {
                    lp = sp + strlen(recog_link);  // drag back lp
                    for (rp = lp; *rp != '"'; ++rp) {
                    }
                    if (*lp != '/') {
                        *(lp - 1) = '/';
                        lp--;
                    }
                    memcpy(local_link, lp, rp - lp);
                }

                sp = rp;

                if (statusFlag == 3 &&
                    search(redirect_dest, hash_djb2(local_link)) == NULL) {
                    insert(redirect_dest, local_link);
                }

                if (is_external_site) {
                    if (search(offsite_host_table, hash_djb2(local_host)) ==
                        NULL) {
                        // int ix = offsite_host_table->current_available;
                        offsite_ports[offsite_host_table->current_available] =
                            local_port[0] != 0 ? atoi(local_port) : -1;
                        // if (search(offsite_dest_table, hash_djb2(local_link))
                        // == NULL) {
                        insert(
                            offsite_dest_table,
                            local_link);  // // duplicate value can be inserted
                        insert(offsite_offer_table,
                               link);  // duplicate value can be inserted
                        insert(offsite_host_table, local_host);
                    }
                    continue;
                }

                if (search(page_table, hash_djb2(local_link)) == NULL) {
                    enqueue(queue, local_link);
                    insert(page_table, local_link);
                }
            }
            // for (int i = 0; i < offsite_host_table->current_available; ++i) {
            //     printf("[%d] -> %s\n", i, offsite_dest_links[i]);
            // }
        }
        // close socket etc and terminate
        close(sockfd);
        freeaddrinfo(server);

        if (is_initial_request) {
            is_initial_request = false;
        }
    }

    printf("%s: closed socket and terminating\n\n", PROG);
    printf("----- Report Items -----\n");

    printf("1.\nTotal number of distinct URLs = %d\n",
           page_table->current_available + img_table->current_available +
               offsite_host_table->current_available);

    printf("2.\nNumber of HTML pages = %d\nNumber of non-HTML objects = %d\n",
           page_table->current_available, img_table->current_available);

    printf("3.\nSmallest page is [http://%s%s], size = %d bytes\n", host_name,
           min_size_page, min_size);
    printf("Largest page is [http://%s%s], size = %d bytes\n", host_name,
           max_size_page, max_size);

    struct tm *odt = localtime(&oldest_t);
    printf("4.\nOldest page is [http://%s%s], timestamp = %s", host_name,
           oldest_page, asctime(odt));
    struct tm *mrt = localtime(&recent_t);
    printf("Most recent-modified page is [http://%s%s], timestamp = %s",
           host_name, most_recent_modified_page, asctime(mrt));

    printf("5.\nInvalid URLs (404):\n");
    for (int i = 0; i < not_found_table->current_available; ++i) {
        printf("[http://%s%s]\n", host_name, not_found_table->items[i]->value);
    }

    printf("6.\nRedirected URLs and destinations (30x):\n");
    for (int i = 0; i < redirect_table->current_available; ++i) {
        printf("[http://%s%s] -> [http://%s%s]\n", host_name,
               redirect_table->items[i]->value, host_name,
               redirect_dest->items[i]->value);
    }

    printf("7.\nOff-site URLs and valid flags:\n");
    for (int i = 0; i < offsite_host_table->current_available; ++i) {
        char *offsite_host = offsite_host_table->items[i]->value;
        printf("[http://%s%s] -> [http://%s", host_name,
               offsite_offer_table->items[i]->value, offsite_host);
        char offsite_port[LINKLEN];
        if (offsite_ports[i] > 0) {
            sprintf(offsite_port, "%d", offsite_ports[i]);
            // printf(":%s%s] -> ", offsite_port, offsite_dest_links[i]);
            printf(":%s%s] | ", offsite_port,
                   offsite_dest_table->items[i]->value);
        } else {
            printf("%s] | ", offsite_dest_table->items[i]->value);
        }

        /* --- check the availability of the external site --- */
        // we don't need "www." (but only the host name) so wipe it off
        char *sp = strstr(offsite_host, "www.");
        if (sp != NULL) {
            sp = sp + strlen("www.");
        }

        bool validFlag = true;

        if ((err = getaddrinfo(offsite_host_table->items[i]->value,
                               offsite_ports[i] > 0 ? offsite_port : "80",
                               &hints, &server))) {
            // use port 80 as default (if port
            // has not been specified)
            // fail to get the address info -> invalid webserver there
            printf("");
            validFlag = false;
        }
        if (validFlag) {
            sockfd = socket(server->ai_family, server->ai_socktype,
                            server->ai_protocol);
            if (sockfd < 0) {
                resourceError(sockfd, "socket");
            }
            if ((err = connect(sockfd, server->ai_addr, server->ai_addrlen)) <
                0) {
                resourceError(err, "connect");
            }
            addrlen = sizeof(serverAddr);
            getsockname(sockfd, (struct sockaddr *)&serverAddr, &addrlen);

            bzero(request, sizeof(request));
            requestHEAD(request, offsite_host);
            nbytes_total = strlen(request);
            nbytes_sent = 0;
            do {
                nbytes = write(sockfd, request + nbytes_sent,
                               nbytes_total - nbytes_sent);
                if (nbytes < 0) {
                    printf("ERROR writing message to socket\n");
                    exit(1);
                }
                if (nbytes == 0) {
                    break;
                }
                nbytes_sent += nbytes;
            } while (nbytes_sent < nbytes_total);

            if (nbytes_total < 0) {
                resourceError(nbytes_total, "send");
            }
            bzero(buf, sizeof(buf));
            bzero(response, sizeof(response));
            nbytes_received = 0;
            sp = NULL;
            do {
                nbytes = recv(sockfd, buf, sizeof(buf), 0);
                if (nbytes < 0) {
                    printf("ERROR receiving message to socket\n");
                    exit(1);
                }
                if (nbytes == 0) {
                    break;
                }
                buf[MIN(nbytes, (sizeof(buf)))] = 0;
                nbytes_received += nbytes;
                strcat(response, buf);
                // check if it's a HTTP response
                sp = strstr(response, "HTTP");
                if (sp != NULL) {
                    break;
                }
            } while (true);
            if (sp == NULL) {
                validFlag = false;
            }
        }
        printf("%s\n", validFlag ? "Valid" : "Invalid");
    }

    return 0;
    
}