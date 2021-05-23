/**
 * @file quicheServer.cxx
 * @version 1.0
 * @author Victor(1294958142@qq.com)
 * @date 2021-04-30
 * @brief
/*********** (C) COPYRIGHT 2021 Victor ***********/

#include <blink/quicheServer.h>
#include <fcntl.h>
#include <inttypes.h>
#include <network/QSocket.h>
#include <rfb/LogWriter.h>
#include <unistd.h>

using namespace quiche;

static rfb::LogWriter vlog("quicheServer");

quiche_config *quiche::quiche_configure_server() {
  quiche_config *config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
  if (config == NULL) {
    throw QuicheException("failed to create quiche config", 0);
  } else {
    quiche_config_load_cert_chain_from_pem_file(config, "./cert.crt");
    quiche_config_load_priv_key_from_pem_file(config, "./cert.key");

    quiche_config_set_application_protos(
        config, (uint8_t *)"\x05hq-28\x05hq-27\x08http/0.9", 21);

    quiche_config_set_max_idle_timeout(config, 5000);
    quiche_config_set_max_packet_size(config, MAX_DATAGRAM_SIZE);
    quiche_config_set_initial_max_data(config, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(config, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(config, 1000000);
    quiche_config_set_initial_max_streams_bidi(config, 100);
    quiche_config_set_cc_algorithm(config, QUICHE_CC_RENO);
    vlog.info("successfully configure quiche");
  }
  return config;
}

void quiche::mint_token(const uint8_t *dcid, size_t dcid_len,
                        struct sockaddr_storage *addr, socklen_t addr_len,
                        uint8_t *token, size_t *token_len) {
  memcpy(token, "quiche", sizeof("quiche") - 1);
  memcpy(token + sizeof("quiche") - 1, addr, addr_len);
  memcpy(token + sizeof("quiche") - 1 + addr_len, dcid, dcid_len);

  *token_len = sizeof("quiche") - 1 + addr_len + dcid_len;
}

bool quiche::validate_token(const uint8_t *token, size_t token_len,
                            struct sockaddr_storage *addr, socklen_t addr_len,
                            uint8_t *odcid, size_t *odcid_len) {
  if ((token_len < sizeof("quiche") - 1) ||
      memcmp(token, "quiche", sizeof("quiche") - 1)) {
    return false;
  }

  token += sizeof("quiche") - 1;
  token_len -= sizeof("quiche") - 1;

  if ((token_len < addr_len) || memcmp(token, addr, addr_len)) {
    return false;
  }

  token += addr_len;
  token_len -= addr_len;

  if (*odcid_len < token_len) {
    return false;
  }

  memcpy(odcid, token, token_len);
  *odcid_len = token_len;

  return true;
}

conn_io *quiche::create_conn_server(uint8_t *odcid, size_t odcid_len,
                                    conn_io *&conns, quiche_config *config) {
  conn_io *conn = new conn_io;
  if (conn == NULL) {
    throw QuicheException("failed to allocate connection IO", 0);
    return NULL;
  }

  int rng = open("/dev/urandom", O_RDONLY);
  if (rng < 0) {
    throw QuicheException("failed to open /dev/urandom", 0);
    return NULL;
  }

  ssize_t rand_len = read(rng, conn->cid, LOCAL_CONN_ID_LEN);
  if (rand_len < 0) {
    throw QuicheException("failed to create connection ID", 0);
    return NULL;
  }

  quiche_conn *q_conn =
      quiche_accept(conn->cid, LOCAL_CONN_ID_LEN, odcid, odcid_len, config);
  if (conn == NULL) {
    throw QuicheException("failed to create connection", 0);
    return NULL;
  }
  conn->q_conn = q_conn;

  HASH_ADD(hh, conns, cid, LOCAL_CONN_ID_LEN, conn);

  vlog.info("new connection");

  return conn;
}