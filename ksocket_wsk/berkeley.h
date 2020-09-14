#pragma once
#include <ntddk.h>
#include <wsk.h>
//#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int       socklen_t;
typedef intptr_t  ssize_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;

uint32_t htonl(uint32_t hostlong);
uint16_t htons(uint16_t hostshort);
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);

int GetAddrInfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res);
void FreeAddrInfo(struct addrinfo *res);

int Socket(int domain, int type, int protocol);
int socket_listen(int domain, int type, int protocol);
int socket_datagram(int domain, int type, int protocol);
int Connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
int Listen(int sockfd, int backlog);
int Bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int Accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int Send(int sockfd, const void* buf, size_t len, int flags, PNTSTATUS error);
int SendTo(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
int Recv(int sockfd, void* buf, size_t len, int flags, PNTSTATUS error);
int RecvFrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int Close(int sockfd);
int Disconnect(int sockfd);

#ifdef __cplusplus
}
#endif
