/**
 * @file quicheConfig.h
 * @version 1.0
 * @author Victor(1294958142@qq.com)
 * @date 2021-04-28
 * @brief
/*********** (C) COPYRIGHT 2021 Victor ***********/

#ifndef _QUICHECONFIG_H_INCLUDED_
#define _QUICHECONFIG_H_INCLUDED_

#include <blink/quiche.h>
#include <rdr/Exception.h>
#include <sys/socket.h>
#include <uthash.h>

namespace quiche {

// - Quiche parameters and structs

#define LOCAL_CONN_ID_LEN 16

#define MAX_DATAGRAM_SIZE 1350

#define MAX_TOKEN_LEN                                      \
  sizeof("quiche") - 1 + sizeof(struct sockaddr_storage) + \
      QUICHE_MAX_CONN_ID_LEN

struct conn_io {
  uint8_t cid[LOCAL_CONN_ID_LEN];  // - Connection id

  quiche_conn* q_conn;  // - Quiche connection

  struct sockaddr_storage peer_addr;  // - Peer address info
  socklen_t peer_addr_len;

  UT_hash_handle hh;  // - For hash table
};

struct QuicheException : public rdr::SystemException {
  QuicheException(const char* text, int err_)
      : rdr::SystemException(text, err_) {}
};

}  // namespace quiche

#endif  // _QUICHECONFIG_H_INCLUDED_
