/**
 * @file quicheServer.h
 * @version 1.0
 * @author Victor(1294958142@qq.com)
 * @date 2021-04-30
 * @brief
/*********** (C) COPYRIGHT 2021 Victor ***********/

#ifndef _QUICHESERVER_H_INCLUDED_
#define _QUICHESERVER_H_INCLUDED_

#include <blink/quicheCommon.h>

namespace quiche {

quiche_config *quiche_configure_server();

void mint_token(const uint8_t *dcid, size_t dcid_len,
                struct sockaddr_storage *addr, socklen_t addr_len,
                uint8_t *token, size_t *token_len);

bool validate_token(const uint8_t *token, size_t token_len,
                    struct sockaddr_storage *addr, socklen_t addr_len,
                    uint8_t *odcid, size_t *odcid_len);

conn_io *create_conn_server(uint8_t *odcid, size_t odcid_len, conn_io *conns,
                            quiche_config *config);

}  // namespace quiche

#endif  // _QUICHESERVER_H_INCLUDED_
